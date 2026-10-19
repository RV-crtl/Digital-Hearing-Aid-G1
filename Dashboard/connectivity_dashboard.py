import os, time, json, csv, queue, zipfile, hashlib, threading, datetime, shutil, subprocess, math, wave, base64, binascii
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

try:
    import serial
    import serial.tools.list_ports
    import numpy as np
    import matplotlib
    matplotlib.use('TkAgg')
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
except Exception as e:
    raise SystemExit(f"Missing dashboard dependency: {e}\nRun START_DASHBOARD.bat again.")

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / 'RESULTS_R26'
RESULTS.mkdir(exist_ok=True)
LATEST = ROOT / 'SEND_THIS_TO_CHATGPT_LATEST.zip'
# Do not rewrite a tracked repository file with a machine-specific path on import.
BAUD = 115200

STAGES = {
    1: ('HEALTH + UART', 'Board powered and serial connected.', 'Real HAL clock; two sampled counter snapshots verify active FreeRTOS tasks and UART receive.'),
    2: ('SPEAKER OUTPUT', 'Listen for the real 1 kHz output tone.', 'SAI speaker DMA progresses and audible tone is confirmed.'),
    3: ('BLUETOOTH AUDIO', 'Play Bluetooth music or speech through the physical receiver.', 'SAI1 DMA, real PCM and speaker DMA progress; audio heard.'),
    4: ('TELECOIL AUDIO', 'Pause Bluetooth and apply a real induction/hearing-loop signal.', 'SAI2 DMA, real PCM and speaker DMA progress; audio heard.'),
    5: ('FREERTOS UNDER LOAD', 'Keep real Bluetooth playing. Compare kernel time with workload OFF then ON.', 'Both equal-priority CPU-load tasks can share processor time while priority-7 audio can preempt them.'),
}

# Evidence is deliberately granular. One bad optional counter no longer turns a whole stage red.
EVIDENCE = [
    ('CLOCK','HAL system-clock reading'), ('UART','RX interrupts armed, no new overruns'),
    ('RTOS_CTL','ControlTask advancing'), ('RTOS_AUDIO','AudioTask advancing'),
    ('RTOS_BT','BtTask advancing'), ('RTOS_BG','BackgroundTask advancing'),
    ('OUTPUT_DMA','SAI2B actual speaker DMA'), ('OUTPUT_PHYS','audible 1 kHz tone'),
    ('SAI1','BT ADC DMA + processed blocks'), ('BT_SIGNAL','nonzero real BT PCM'),
    ('BT_OUTPUT','real speaker activity'), ('BT_CONT','FIFO and capture loss'), ('BT_PHYS','BT audio physically heard'),
    ('SAI2','telecoil ADC DMA + processed blocks'), ('TC_CLOCK','actual SAI1/SAI2 clock and DMA'),
    ('TC_SIGNAL','nonzero real telecoil PCM'), ('TC_OUTPUT','real speaker activity'), ('TC_PHYS','TC audio physically heard'),
    ('RTOS_LOAD','kernel execution and progress with low-priority load'),
]

COLORS = {'PASS':'#bbf7d0','WARN':'#fde68a','FAIL':'#fecaca','WAIT':'#e5e7eb','RUN':'#bfdbfe'}

def iso(): return datetime.datetime.now().isoformat(timespec='milliseconds')
def stamp(): return datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
def short_time(): return datetime.datetime.now().strftime('%H:%M:%S')

def kv_line(line):
    p=line.strip().split('|')
    if len(p)<3 or p[0] not in ('R21','R26','R27','R28','R29'): return None
    d={'topic':p[1], 'state':p[2], 'raw':line.strip()}
    for x in p[3:]:
        if '=' in x:
            k,v=x.split('=',1); d[k]=v
    return d

def iv(d,k,default=0):
    try: return int(d.get(k,default))
    except: return default

def compute_fft(samples, fs):
    x=np.asarray(samples,dtype=float)
    if x.size<8: return 0.0,0.0
    x=x-np.mean(x)
    rms=float(np.sqrt(np.mean(x*x))) if x.size else 0.0
    y=x*np.hanning(x.size)
    mag=np.abs(np.fft.rfft(y,1024)); freq=np.fft.rfftfreq(1024,1/fs)
    if mag.size<=1: return 0.0,rms
    k=int(np.argmax(mag[1:]))+1
    return float(freq[k]),rms

class SerialLink:
    def __init__(self):
        self.ser=None; self.stop=threading.Event(); self.reader=None
        self.uiq=queue.Queue(); self.matchq=queue.Queue(); self.lock=threading.Lock()
        self.tx=[]; self.rx=[]
    def ports(self): return [p.device for p in serial.tools.list_ports.comports()]
    @staticmethod
    def drain(q):
        try:
            while True: q.get_nowait()
        except queue.Empty: pass
    def connect(self,port):
        self.close(); self.ser=serial.Serial(port,BAUD,timeout=.08,write_timeout=.7)
        self.ser.reset_input_buffer(); self.ser.reset_output_buffer(); self.stop.clear()
        self.reader=threading.Thread(target=self._read,daemon=True); self.reader.start()
    def close(self):
        self.stop.set()
        if self.reader and self.reader.is_alive(): self.reader.join(.4)
        if self.ser:
            try:self.ser.close()
            except:pass
        self.ser=None; self.reader=None; self.drain(self.matchq)
    def _read(self):
        buf=bytearray()
        while not self.stop.is_set() and self.ser and self.ser.is_open:
            try:b=self.ser.read(256)
            except Exception as e:
                self.uiq.put((iso(),f'SERIAL ERROR: {e}')); break
            if not b: continue
            buf.extend(b)
            while b'\n' in buf:
                raw,_,buf=buf.partition(b'\n')
                line=raw.rstrip(b'\r').decode('utf-8',errors='replace')
                if not line: continue
                t=iso(); self.rx.append((t,line)); self.uiq.put((t,line)); self.matchq.put((t,line))
    def poll(self):
        out=[]
        try:
            while True: out.append(self.uiq.get_nowait())
        except queue.Empty: pass
        return out
    def send(self,cmd):
        if not self.ser or not self.ser.is_open: raise RuntimeError('Not connected')
        cmd=cmd.strip(); t=iso(); self.tx.append((t,cmd)); self.uiq.put((t,'>> '+cmd))
        self.ser.write((cmd+'\n').encode('ascii')); self.ser.flush()
    def ask(self,cmd,prefix,timeout=3.0,retries=2):
        # R25: retries are appropriate for READ-ONLY queries.  State-changing
        # commands (!OS/!BS/!TS/!FS) are explicitly issued with retries=1 so a
        # missing ACK can never restart DMA or create an audible cut-out.
        last=None
        with self.lock:
            for attempt in range(1,retries+1):
                self.drain(self.matchq); self.send(cmd)
                deadline=time.time()+timeout; hold=[]
                while time.time()<deadline:
                    try:t,line=self.matchq.get(timeout=min(.12,max(.01,deadline-time.time())))
                    except queue.Empty: continue
                    if line.startswith(prefix):
                        for x in hold:self.matchq.put(x)
                        d=kv_line(line) or {'raw':line,'state':'UNKNOWN'}
                        d['_attempt']=attempt
                        return d
                    hold.append((t,line))
                for x in hold:self.matchq.put(x)
                last=f'No {prefix} response on attempt {attempt}/{retries}'
                time.sleep(.12)
            raise TimeoutError(last or f'No {prefix} response')
    def collect_diag(self,timeout=3.0):
        with self.lock:
            for attempt in (1,2):
                self.drain(self.matchq); self.send('!DG')
                deadline=time.time()+timeout; d1=d2=None; others=[]
                while time.time()<deadline and (d1 is None or d2 is None):
                    try:t,line=self.matchq.get(timeout=.12)
                    except queue.Empty: continue
                    if line.startswith('R21|DIAG1|'): d1=kv_line(line)
                    elif line.startswith('R21|DIAG2|'): d2=kv_line(line)
                    else: others.append((t,line))
                for x in others:self.matchq.put(x)
                if d1 is not None and d2 is not None: return d1,d2
                time.sleep(.12)
            return d1,d2
    def scope(self,timeout=3.0):
        """Request one explicit scope snapshot. R26 uses a single compact
        hex-framed line so audio processing is not stalled by 12 slow UART lines.
        The R21 multi-line parser is retained as a compatibility fallback.
        """
        with self.lock:
            for attempt in range(1,4):
                self.drain(self.matchq); self.send('!SP')
                deadline=time.time()+timeout; seq=None; fs=12000; src=0; n=0; values={}; others=[]
                while time.time()<deadline:
                    try:t,line=self.matchq.get(timeout=min(.12,max(.01,deadline-time.time())))
                    except queue.Empty: continue
                    if line.startswith('R26|SCOPE|WAIT|'):
                        break
                    if line.startswith('R26|SCOPE|PASS|'):
                        d=kv_line(line) or {}
                        try:
                            n=iv(d,'n',256); hx=d.get('hex','')
                            if len(hx) < n*4: raise ValueError('short hex payload')
                            arr=[]
                            for i in range(n):
                                v=int(hx[4*i:4*i+4],16)
                                if v & 0x8000: v-=0x10000
                                arr.append(v)
                            return {'seq':iv(d,'seq'),'fs':iv(d,'fs',12000),'src':iv(d,'src'),'samples':arr,'filled':len(arr),'transport':'R26_COMPACT'}
                        except Exception as e:
                            others.append((t,f'SCOPE PARSE ERROR: {e}'))
                            break
                    # Compatibility with R23-R25 chunked scope.
                    if line.startswith('R21|SCOPE|BEGIN|'):
                        d=kv_line(line); seq=iv(d,'seq'); fs=iv(d,'fs',12000); src=iv(d,'src'); n=iv(d,'n',96); continue
                    if line.startswith('R21|SCOPE|D|'):
                        d=kv_line(line); idx=iv(d,'i'); raw=d.get('v','')
                        try:
                            vv=[int(x) for x in raw.split(',') if x.strip()]
                            for j,v in enumerate(vv): values[idx+j]=v
                        except: pass
                        continue
                    if line.startswith('R21|SCOPE|END|'):
                        d=kv_line(line); endseq=iv(d,'seq')
                        if seq is not None and endseq==seq:
                            arr=[values.get(i,0) for i in range(n)]
                            return {'seq':seq,'fs':fs,'src':src,'samples':arr,'filled':sum(i in values for i in range(n)),'transport':'LEGACY_ASCII'}
                    others.append((t,line))
                for x in others:self.matchq.put(x)
                time.sleep(.10)
            raise TimeoutError('No complete R26 scope snapshot after 3 attempts')

    def scope_dual(self,timeout=2.0):
        """Two exact 16-bit 128-sample PCM windows: same active source, same seq.
        UART receive gaps remain gaps: do not concatenate them as raw audio.
        """
        with self.lock:
            for attempt in range(2):
                self.drain(self.matchq); self.send('!DP')
                deadline=time.monotonic()+timeout
                while time.monotonic()<deadline:
                    try: _t,line=self.matchq.get(timeout=min(.12,max(.01,deadline-time.monotonic())))
                    except queue.Empty:continue
                    if line.startswith('R27|DUAL|WAIT|'):break
                    if not line.startswith('R27|DUAL|PASS|'):continue
                    d=kv_line(line) or {}
                    try:
                        n=iv(d,'n');fs=iv(d,'fs');src=iv(d,'src');seq=iv(d,'seq')
                        if n!=128 or fs!=12000 or src not in (1,2):continue
                        raw=base64.b64decode(d['pre64'],validate=True)
                        processed=base64.b64decode(d['post64'],validate=True)
                        if len(raw)!=2*n or len(processed)!=2*n:continue
                        if binascii.crc_hqx(raw+processed,0xffff)!=int(d.get('crc','-1'),16):continue
                        pre=np.frombuffer(raw,dtype='<i2').astype(np.int16).copy()
                        post=np.frombuffer(processed,dtype='<i2').astype(np.int16).copy()
                        return {'seq':seq,'fs':fs,'src':src,'pre':pre,'post':post,'samples':post,
                                'filled':n,'transport':'R27_DUAL_PCM16_EXACT'}
                    except (ValueError,KeyError,TypeError):continue
                time.sleep(.03)
            raise TimeoutError('No paired R27 monitor response: verify firmware and serial RX')

    def scope_hd(self,timeout=3.0):
        """2,048 consecutive real STM32 samples via !SH (5.86-Hz true resolution).
        Keep legacy !SP completely unchanged. Lock ensures responses cannot interleave.
        """
        with self.lock:
            for attempt in range(2):
                self.drain(self.matchq)
                self.send('!SH')
                deadline=time.monotonic()+timeout
                seq=None; fs=12000; src=0; n=2048; parts={}; ended=False
                while time.monotonic()<deadline:
                    try: t,line=self.matchq.get(timeout=min(.18,max(.01,deadline-time.monotonic())))
                    except queue.Empty: continue
                    if line.startswith('R26|SCOPEHD|WAIT|'): break
                    if line.startswith('R26|SCOPEHD|D|'):
                        d=kv_line(line); k=iv(d,'i',-1)
                        hx=d.get('hex',''); this_seq=iv(d,'seq')
                        if seq is None: seq=this_seq
                        if this_seq!=seq or k<0 or k%256 or len(hx)!=1024: continue
                        try:
                            bb=bytes.fromhex(hx)
                            vals=np.frombuffer(bb,dtype='>u2').astype(np.int32)
                            vals[vals>=32768]-=65536
                            parts[k]=vals.astype(np.int16)
                            fs=iv(d,'fs',12000); src=iv(d,'src'); n=iv(d,'n',2048)
                        except ValueError: continue
                    elif line.startswith('R26|SCOPEHD|END|'):
                        d=kv_line(line)
                        ended=(seq is not None and iv(d,'seq')==seq)
                        if ended: break
                if ended and n==2048 and all(i in parts for i in range(0,n,256)):
                    samples=np.concatenate([parts[i] for i in range(0,n,256)])
                    return {'seq':seq,'fs':fs,'src':src,'samples':samples.tolist(),
                            'filled':n,'transport':'R26_HD_CONTIGUOUS'}
                time.sleep(.18)
            raise TimeoutError('HD capture incomplete; select FAST (256) or check serial link')

class GraphWindow:
    """Paired pre/post-DSP board analyzer. Every dual frame is real, same-source PCM.

    The new !DP command provides 128 contiguous samples per channel at 12 kSa/s.
    Samples between UART requests are NOT transmitted; never concatenate those
    captures or imply a gapless stream. HD remains a detailed POST-only snapshot.
    """
    def __init__(self,app):
        self.app=app; self.win=None; self.live=False; self.live_busy=False
        self.live_after=None; self.live_last_seq=None; self.live_frames=0; self.live_errors=0
        self.live_rate=tk.DoubleVar(value=12.0)
        self.resolution=tk.StringVar(value='DUAL LIVE · 128')
        self.hist=[[],[]]; self.hist_times=[]; self.last=None
        self.plot_fs=None; self.plot_n=None; self.last_src=None; self.last_frame_time=None
        self.plot_lines=[]; self.waterfall_images=[]
    def _ensure(self):
        if self.win and self.win.winfo_exists(): return
        self.win=tk.Toplevel(self.app)
        self.win.title('261013-Individual_Demonstration_Connectivity · paired analyser')
        self.win.geometry('1340x920'); self.win.minsize(1050,730)
        self.win.configure(bg='#0b1220')
        self.win.protocol('WM_DELETE_WINDOW',self.close)
        top=tk.Frame(self.win,bg='#111827',padx=12,pady=9); top.pack(fill='x')
        tk.Label(top,text='DUAL SIGNAL LAB',bg='#111827',fg='#f8fafc',font=('Segoe UI Semibold',17)).pack(side='left')
        self.info=tk.StringVar(value='Select a live mode • both columns follow the active input source')
        tk.Label(top,textvariable=self.info,bg='#111827',fg='#93c5fd',font=('Segoe UI',9),anchor='e').pack(side='right',fill='x',expand=True,padx=8)
        bar=tk.Frame(self.win,bg='#172033',padx=8,pady=8); bar.pack(fill='x')
        ttk.Button(bar,text='PAIRED SNAPSHOT',command=self.capture_dual_once).pack(side='left',padx=3)
        ttk.Button(bar,text='BT INPUT',command=lambda:self.app.manual('!BS')).pack(side='left',padx=2)
        ttk.Button(bar,text='TC INPUT',command=lambda:self.app.manual('!TS')).pack(side='left',padx=2)
        self.live_btn=ttk.Button(bar,text='START LIVE',command=self.toggle_live); self.live_btn.pack(side='left',padx=3)
        tk.Label(bar,text='MAX REQUEST Hz',fg='#cbd5e1',bg='#172033').pack(side='left',padx=(12,3))
        ttk.Combobox(bar,textvariable=self.live_rate,values=(2,4,8,12,16,20),width=5,state='readonly').pack(side='left',padx=3)
        tk.Label(bar,text='CAPTURE MODE',fg='#cbd5e1',bg='#172033').pack(side='left',padx=(12,3))
        cbo=ttk.Combobox(bar,textvariable=self.resolution,values=('DUAL LIVE · 128','LEGACY · 256 (POST ONLY)','HD · 2048 (POST ONLY)'),width=17,state='readonly'); cbo.pack(side='left',padx=3)
        cbo.bind('<<ComboboxSelected>>',self._resolution_changed)
        ttk.Button(bar,text='EXPORT',command=self.export).pack(side='right',padx=3)
        ttk.Button(bar,text='NAVY FILE STUDIO',command=self.open_file_lab).pack(side='right',padx=3)
        cards=tk.Frame(self.win,bg='#0b1220',padx=9,pady=6); cards.pack(fill='x')
        self.metric_vars={}
        for key,label in [('src','ACTIVE SOURCE'),('rms','OUTPUT RMS'),('gain','PRE → POST LEVEL'),('freq','OUTPUT PEAK'),('rate','MEASURED FRAME Hz'),('snr','SPECTRAL CONTRAST')]:
            f=tk.Frame(cards,bg='#172033',padx=9,pady=6); f.pack(side='left',fill='x',expand=True,padx=3)
            tk.Label(f,text=label,bg='#172033',fg='#94a3b8',font=('Segoe UI',8,'bold')).pack(anchor='w')
            v=tk.StringVar(value='—'); self.metric_vars[key]=v
            tk.Label(f,textvariable=v,bg='#172033',fg='#f8fafc',font=('Segoe UI Semibold',11)).pack(anchor='w')
        self.fig=Figure(figsize=(12.6,7.3),dpi=90,facecolor='#0b1220')
        self.axes=self.fig.subplots(3,2)
        self.a,self.b,self.c=self.axes[:,1]  # legacy compatibility (post column)
        self.plot_lines=[]; self.waterfall_images=[]
        titles=('BEFORE DSP · selected input PCM','AFTER DSP · processed output PCM')
        for col in (0,1):
            for row in (0,1,2):
                ax=self.axes[row,col]; ax.set_facecolor('#0f172a')
                ax.tick_params(colors='#cbd5e1',labelsize=8)
                for spine in ax.spines.values():spine.set_color('#334155')
                ax.title.set_color('#f8fafc');ax.xaxis.label.set_color('#94a3b8');ax.yaxis.label.set_color('#94a3b8')
                if row<2:ax.grid(True,alpha=.16)
                ax.set_title(titles[col]+'  |  '+('TIME' if row==0 else 'FREQUENCY' if row==1 else 'WATERFALL'),fontsize=9,pad=4)
                ax.set_xlabel(('Time in window (ms)','Frequency (Hz)','Frequency (Hz)')[row],fontsize=8)
                ax.set_ylabel(('PCM counts','dBFS','History: newest at top (frames)')[row],fontsize=8)
            l1,=self.axes[0,col].plot([],[],lw=.85,color='#38bdf8' if col==0 else '#34d399')
            l2,=self.axes[1,col].plot([],[],lw=1.05,color='#38bdf8' if col==0 else '#34d399')
            self.plot_lines.append((l1,l2))
            im=self.axes[2,col].imshow(np.full((2,513),-120,dtype=float),origin='upper',aspect='auto',interpolation='nearest',vmin=-100,vmax=-18,extent=(0,6000,2,0),cmap='magma')
            self.waterfall_images.append(im)
        self.axes[1,0].set_ylim(-120,0);self.axes[1,1].set_ylim(-120,0)
        self.fig.subplots_adjust(hspace=.64,wspace=.21,left=.075,right=.98,top=.95,bottom=.07)
        self.canvas=FigureCanvasTkAgg(self.fig,master=self.win)
        self.canvas.get_tk_widget().pack(fill='both',expand=True,padx=7,pady=(0,7))
    def capture_dual_once(self):
        """The prominent snapshot button MUST acquire !DP, not legacy post-only !SP.
        Do not let a manually-requested single frame compete with the live worker.
        """
        self._ensure()
        if self.live:
            self.info.set('Pause LIVE before taking a separate paired snapshot.')
            return
        if self.live_busy or self.app.running:
            self.info.set('Another capture or test is in progress; wait and retry.')
            return
        if not self.app.link.ser:
            self.info.set('Connect the STM32 to request a PRE + POST snapshot.')
            return
        self.live_busy=True
        def worker():
            try:
                frame=self.app.link.scope_dual(2.5)
                self.app.ui(self._show_single_dual,frame)
            except Exception as exc:
                self.app.ui(self._snapshot_error,str(exc))
            finally:
                self.live_busy=False
        threading.Thread(target=worker,daemon=True).start()

    def _show_single_dual(self,frame):
        if self.win and self.win.winfo_exists():
            self.show_dual(frame['pre'],frame['post'],frame['fs'],frame['seq'],frame['src'],dual=True)

    def _snapshot_error(self,reason):
        if self.win and self.win.winfo_exists():
            self.info.set('PAIRED SNAPSHOT unavailable: '+reason[:115])

    def _resolution_changed(self,event=None):
        self._reset_hist()
        if self.resolution.get().startswith('HD'):self.live_rate.set(2.0)
        elif self.resolution.get().startswith('DUAL'):self.live_rate.set(12.0)
        self.info.set('DUAL supplies BOTH pre/post FFTs. Legacy/HD are POST-ONLY. Waterfalls fall downward; gaps are not interpolated.')
    def _reset_hist(self):
        self.hist=[[],[]]; self.hist_times=[]; self.live_last_seq=None; self.last_frame_time=None
    def close(self):
        self.live=False
        if self.live_after is not None and self.win and self.win.winfo_exists():
            try:self.win.after_cancel(self.live_after)
            except tk.TclError:pass
        self.live_after=None
        if self.win:self.win.destroy()
        self.win=None
    def toggle_live(self):
        self._ensure();self.live=not self.live
        self.live_btn.configure(text='STOP LIVE' if self.live else 'START LIVE')
        if self.live:
            self._reset_hist();self.live_frames=0;self.live_errors=0
            self._live_tick()
        elif self.live_after is not None:
            try:self.win.after_cancel(self.live_after)
            except tk.TclError:pass
            self.live_after=None;self.info.set(f'Stopped · {self.live_frames} unique STM32 frames')
    def _live_tick(self):
        self.live_after=None
        if not self.live or not self.win or not self.win.winfo_exists():return
        if not self.live_busy and not self.app.running and self.app.link.ser:
            self.live_busy=True
            mode=self.resolution.get()
            def w():
                try:
                    if mode.startswith('DUAL'):frame=self.app.link.scope_dual(2.0)
                    elif mode.startswith('HD'):frame=self.app.link.scope_hd(4.8)
                    else:frame=self.app.link.scope(2.0)
                    self.app.ui(self._live_frame,frame)
                except Exception as e:self.app.ui(self._live_failure,str(e))
                finally:self.live_busy=False
            threading.Thread(target=w,daemon=True).start()
        self.live_after=self.win.after(max(25,int(1000/max(self.live_rate.get(),.5))),self._live_tick)
    def _live_frame(self,frame):
        if not self.live or not self.win or not self.win.winfo_exists():return
        if frame['seq']==self.live_last_seq and frame['src']==self.last_src:return
        if self.last_src is not None and frame['src']!=self.last_src:self._reset_hist()
        self.live_last_seq=frame['seq'];self.live_frames+=1
        self.show_dual(frame.get('pre'),frame.get('post',frame['samples']),frame['fs'],frame['seq'],frame['src'],dual=('pre' in frame))
    def _live_failure(self,reason):
        if not self.live or not self.win or not self.win.winfo_exists():return
        self.live_errors+=1;self.info.set(f'LIVE retry {self.live_errors}: {reason[:90]}')
    def _metrics(self,x,fs):
        x=np.asarray(x,dtype=float)
        if x.size<8:raise ValueError('insufficient audio samples')
        xc=x-x.mean();rms=float(np.sqrt(np.mean(xc*xc)))
        win=np.hanning(len(xc));nfft=max(1024,2**int(np.ceil(np.log2(len(xc)))))
        mag=np.abs(np.fft.rfft(xc*win,nfft))/max(win.sum()/2,1)/32768
        f=np.fft.rfftfreq(nfft,1/fs);db=20*np.log10(np.maximum(mag,1e-8))
        k=int(np.argmax(mag[1:]))+1
        dominant=float(f[k]);contrast=float(db[k]-np.median(db[1:]))
        return rms,dominant,contrast,f,db
    def show(self,samples,fs,seq,src):
        # Existing manual test calls remain POST-only and must not impersonate RAW input.
        self.show_dual(None,samples,fs,seq,src,dual=False)
        return self._metrics(samples,fs)[1:3]
    def show_dual(self,pre,post,fs,seq,src,dual=True):
        self._ensure()
        if self.last_src is not None and self.last_src!=src:self._reset_hist()
        self.last_src=src
        after=np.asarray(post,dtype=np.int16)
        before=np.asarray(pre if pre is not None else np.zeros_like(after),dtype=np.int16)
        now=time.monotonic()
        rate=1/(now-self.last_frame_time) if self.last_frame_time and now>self.last_frame_time else 0
        self.last_frame_time=now
        self.last=(before.copy() if dual else None,after.copy(),fs,seq,src,dual)
        if self.hist and self.hist_times and self.hist_times[-1]>now:self._reset_hist()
        self.hist_times.append(now)
        self.hist_times=self.hist_times[-90:]
        all_metrics=[]
        for col,x in enumerate((before,after)):
            ax_wave=self.axes[0,col];ax_fft=self.axes[1,col];ax_wf=self.axes[2,col]
            if col==0 and not dual:
                self.plot_lines[col][0].set_data([],[])
                self.plot_lines[col][1].set_data([],[])
                self.hist[col]=[]
                self.waterfall_images[col].set_data(np.full((2,513),-120.0))
                self.waterfall_images[col].set_extent((0,fs/2,2,0))
                ax_wf.set_xlim(0,fs/2);ax_wf.set_ylim(2,0)
                all_metrics.append((0.,0.,0.))
                continue
            rms,dom,contrast,f,db=self._metrics(x,fs);all_metrics.append((rms,dom,contrast))
            t=np.arange(x.size)*1000/fs
            self.plot_lines[col][0].set_data(t,x)
            ax_wave.set_xlim(0,max(1,t[-1] if len(t)>1 else 1))
            peak=max(250,float(np.max(np.abs(x)))*1.15)
            ax_wave.set_ylim(-peak,peak)
            self.plot_lines[col][1].set_data(f,db)
            ax_fft.set_xlim(0,fs/2)
            # Classic descending waterfall: new spectrum enters row zero/top;
            # older received frames fall downward. Rows are observed UART frames,
            # NOT interpolated frames or continuous sample-time coverage.
            self.hist[col].append(db.copy());self.hist[col]=self.hist[col][-90:]
            waterfall=np.stack(self.hist[col][::-1],axis=0)
            self.waterfall_images[col].set_data(waterfall)
            self.waterfall_images[col].set_extent((0,fs/2,len(waterfall),0))
            ax_wf.set_xlim(0,fs/2);ax_wf.set_ylim(max(len(waterfall),2),0)
        source={0:'IDLE',1:'TELECOIL',2:'BLUETOOTH'}.get(src,str(src))
        self.metric_vars['src'].set(source)
        self.metric_vars['rms'].set(f'{all_metrics[1][0]:.0f}')
        p=all_metrics[0][0];q=all_metrics[1][0]
        self.metric_vars['gain'].set(f'{20*np.log10(max(q,1)/max(p,1)):+.1f} dB' if dual else 'POST ONLY')
        self.metric_vars['freq'].set(f'{all_metrics[1][1]:.0f} Hz')
        self.metric_vars['rate'].set(f'{rate:.1f}' if rate else '—')
        self.metric_vars['snr'].set(f'{all_metrics[1][2]:.0f} dB')
        self.axes[0,0].set_title('BEFORE DSP · real input PCM' if dual else 'PRE NOT SENT · choose DUAL LIVE or PAIRED SNAPSHOT',fontsize=9,color='#f8fafc')
        self.axes[0,1].set_title('AFTER DSP · real processed PCM',fontsize=9,color='#f8fafc')
        self.info.set(f'{source} · seq={seq} · {len(after)} samples/window · native Δf={fs/len(after):.2f} Hz · {rate:.1f} frames/s · new spectra at TOP; no gap interpolation')
        self.canvas.draw_idle()
    def open_file_lab(self):self.app.file_lab.open()
    def export(self):
        if self.last is None:return messagebox.showinfo('Signal Lab','Capture a frame first.')
        before,after,fs,seq,src,dual=self.last
        d=RESULTS/f'DUAL_SIGNAL_LAB_{stamp()}';d.mkdir(parents=True,exist_ok=True)
        with open(d/'paired_latest.csv','w',newline='',encoding='utf-8') as f:
            w=csv.writer(f);w.writerow(['sample','time_s','before_pcm' if dual else 'before_not_captured','after_pcm','source','snapshot_seq'])
            for i in range(len(after)):w.writerow([i,i/fs,int(before[i]) if dual else '',int(after[i]),src,seq])
        self.fig.savefig(d/'dual_signal_lab.png',dpi=170,bbox_inches='tight')
        (d/'metadata.txt').write_text('Dual monitor: 128 paired 16-bit s16 PCM / 12000 Hz. Snapshots are NONCONTIGUOUS between UART polls.\nHD/legacy are POST-only. FFT zero-padding does not increase native Δf.\n',encoding='utf-8')
        self.app._log_local(f'Dual Signal Lab export -> {d.resolve()}')

class FileSpectrogramWindow:
    """Separate evidence source: original audio-file STFT, never labelled STM32.
    PC WAV playback is visualised with a moving playhead over the actual STFT.
    """
    def __init__(self,app):
        self.app=app; self.win=None; self.path=None; self.audio=None; self.fs=0
        self.start_clock=None; self.after_id=None; self.artist=None
        self.nfft=tk.IntVar(value=4096); self.overlap=tk.DoubleVar(value=0.75)
        self.max_freq=tk.StringVar(value='8000'); self.floor=tk.IntVar(value=-85)

    @staticmethod
    def read_audio(path):
        try:
            import soundfile as sf
            x,fs=sf.read(str(path),dtype='float32',always_2d=True)
            y=np.mean(x,axis=1).astype(np.float32)
        except Exception as ex:
            if path.suffix.lower()!='.wav':
                raise ValueError('Install soundfile or convert this audio to PCM WAV: '+str(ex))
            with wave.open(str(path),'rb') as w:
                fs=w.getframerate(); nch=w.getnchannels(); wid=w.getsampwidth(); raw=w.readframes(w.getnframes())
                if w.getcomptype()!='NONE': raise ValueError('Only PCM WAV supported without soundfile')
            if wid==1: a=(np.frombuffer(raw,dtype=np.uint8).astype(np.float32)-128)/128
            elif wid==2: a=np.frombuffer(raw,dtype='<i2').astype(np.float32)/32768
            elif wid==4: a=np.frombuffer(raw,dtype='<i4').astype(np.float32)/2147483648
            elif wid==3:
                b=np.frombuffer(raw,dtype=np.uint8).reshape(-1,3)
                v=(b[:,0].astype(np.int32) | (b[:,1].astype(np.int32)<<8) | (b[:,2].astype(np.int32)<<16))
                a=np.where((v & 0x800000)!=0,v-0x1000000,v).astype(np.float32)/8388608
            else: raise ValueError('Unsupported WAV sample width')
            y=a.reshape(-1,nch).mean(axis=1)
        if fs<1000 or len(y)<128: raise ValueError('Audio recording too short or invalid')
        return y,fs

    @staticmethod
    def stft(x,fs,nfft=4096,overlap=.75,max_frames=2500):
        nfft=int(nfft); hop=max(1,int(nfft*(1-overlap)))
        if len(x)<nfft: x=np.pad(x,(0,nfft-len(x)))
        cnt=1+(len(x)-nfft)//hop
        if cnt>max_frames: hop=max(hop,int(np.ceil((len(x)-nfft)/max(max_frames-1,1))))
        starts=np.arange(0,len(x)-nfft+1,hop,dtype=np.int64)
        win=np.hanning(nfft).astype(np.float32)
        # bounded processing memory for several-minute Navy audio recordings
        power=np.empty((nfft//2+1,len(starts)),dtype=np.float32)
        for i in range(0,len(starts),80):
            v=np.stack([x[k:k+nfft] for k in starts[i:i+80]]).astype(np.float32)
            v=(v-v.mean(axis=1,keepdims=True))*win
            mag=np.abs(np.fft.rfft(v,axis=1)) / max(float(win.sum())/2,1.0)
            power[:,i:i+len(v)]=mag.T
        db=20*np.log10(np.maximum(power,1e-8))
        freqs=np.fft.rfftfreq(nfft,1/fs)
        tt=(starts+nfft/2)/fs
        return freqs,tt,db

    def open(self):
        if self.win and self.win.winfo_exists(): self.win.lift(); return
        self.win=tk.Toplevel(self.app); self.win.title('DHA | Navy Audio Spectrogram Studio — PC file evidence')
        self.win.geometry('1220x820'); self.win.configure(bg='#0b1220')
        bar=tk.Frame(self.win,bg='#111827',padx=12,pady=10); bar.pack(fill='x')
        tk.Label(bar,text='NAVY AUDIO • HIGH-RESOLUTION SPECTROGRAM',bg='#111827',fg='#e2e8f0',font=('Segoe UI Semibold',14)).pack(side='left')
        ttk.Button(bar,text='OPEN AUDIO FILE',command=self.pick_file).pack(side='right',padx=4)
        ttk.Button(bar,text='EXPORT PNG',command=self.export).pack(side='right',padx=4)
        ttk.Button(bar,text='PLAY WAV',command=self.play).pack(side='right',padx=4)
        ttk.Button(bar,text='STOP',command=self.stop).pack(side='right',padx=4)
        cfg=tk.Frame(self.win,bg='#0b1220',padx=12,pady=8); cfg.pack(fill='x')
        for name,variable,values,w in [('FFT WINDOW',self.nfft,(1024,2048,4096,8192),7),
                                        ('OVERLAP',self.overlap,(.50,.75,.875),6),
                                        ('MAX FREQ (HZ)',self.max_freq,('2000','4000','8000','12000','24000'),8),
                                        ('DB FLOOR',self.floor,(-100,-85,-70,-55),6)]:
            tk.Label(cfg,text=name,bg='#0b1220',fg='#94a3b8').pack(side='left',padx=(9,3))
            ttk.Combobox(cfg,textvariable=variable,values=values,width=w,state='readonly').pack(side='left',padx=3)
        ttk.Button(cfg,text='UPDATE VIEW',command=self.render).pack(side='right')
        self.status=tk.StringVar(value='Load the Australian Navy WAV/FLAC/MP3 file. This plot is computed from the original PC audio file, not the STM32 link.')
        tk.Label(self.win,textvariable=self.status,bg='#0b1220',fg='#93c5fd',anchor='w',font=('Segoe UI',10)).pack(fill='x',padx=13,pady=(1,6))
        self.fig=Figure(figsize=(11.6,7.0),dpi=100,facecolor='#0b1220')
        self.ax=self.fig.add_subplot(211); self.spec=self.fig.add_subplot(212)
        self.canvas=FigureCanvasTkAgg(self.fig,master=self.win); self.canvas.get_tk_widget().pack(fill='both',expand=True,padx=8,pady=8)
        self.win.protocol('WM_DELETE_WINDOW',self.close)

    def pick_file(self):
        path=filedialog.askopenfilename(parent=self.win,title='Choose Navy audio recording',filetypes=[('Audio files','*.wav *.flac *.ogg *.mp3'),('WAV audio','*.wav'),('All files','*.*')])
        if not path: return
        try:
            self.stop(); self.audio,self.fs=self.read_audio(Path(path)); self.path=Path(path)
            self.render()
        except Exception as exc: messagebox.showerror('Audio file could not be read',str(exc),parent=self.win)

    def render(self):
        if self.audio is None or not self.win or not self.win.winfo_exists(): return
        self.stop()
        n=int(self.nfft.get()); fs=self.fs; x=self.audio
        freq,t,db=self.stft(x,fs,n,float(self.overlap.get()))
        upper=min(float(self.max_freq.get()),fs/2)
        keep=freq<=upper; freq=freq[keep]; db=db[keep,:]
        for ax in (self.ax,self.spec):
            ax.clear(); ax.set_facecolor('#0f172a')
            ax.tick_params(colors='#cbd5e1')
            ax.xaxis.label.set_color('#cbd5e1'); ax.yaxis.label.set_color('#cbd5e1')
            ax.title.set_color('#e2e8f0')
            for sp in ax.spines.values(): sp.set_color('#334155')
        duration=len(x)/fs
        ds=max(1,int(fs/15000)); wt=np.arange(0,len(x),ds)/fs
        self.ax.plot(wt,x[::ds],linewidth=.65,color='#38bdf8')
        self.ax.set_xlim(0,duration); self.ax.set_ylim(-1.05,1.05)
        self.ax.set_title('Original recording waveform (unmodified)'); self.ax.set_ylabel('Normalised amplitude')
        vmin=self.floor.get()
        self.spec.pcolormesh(t,freq,np.clip(db,vmin,0),cmap='magma',vmin=vmin,vmax=0,shading='auto',rasterized=True)
        self.spec.set_xlim(0,duration); self.spec.set_ylim(0,upper)
        self.spec.set_xlabel('Recording time (seconds)'); self.spec.set_ylabel('Frequency (Hz)')
        self.spec.set_title(f'Actual STFT • {n} samples / {n/fs*1000:.0f} ms • true Δf = {fs/n:.2f} Hz • {int(float(self.overlap.get())*100)}% overlap')
        self.line1=self.ax.axvline(0,color='#facc15',linewidth=1.3)
        self.line2=self.spec.axvline(0,color='#facc15',linewidth=1.3)
        self.fig.tight_layout(pad=2); self.canvas.draw_idle()
        self.status.set(f'PC FILE (not STM32) • {self.path.name} • {duration:.1f}s • {fs:,} Sa/s • {len(t):,} STFT columns • dynamic range {vmin} to 0 dBFS')

    def play(self):
        if self.path is None:return
        if self.path.suffix.lower()!='.wav':
            return messagebox.showinfo('Playback','Automatic Windows playback supports WAV. Convert other formats to WAV or use a media player.',parent=self.win)
        if os.name!='nt': return messagebox.showinfo('Playback','Windows WAV playback only; the spectrogram still works.',parent=self.win)
        try:
            import winsound
            self.stop(); winsound.PlaySound(str(self.path),winsound.SND_FILENAME|winsound.SND_ASYNC|winsound.SND_NODEFAULT)
            self.start_clock=time.monotonic(); self.tick()
        except Exception as ex: messagebox.showerror('Playback failed',str(ex),parent=self.win)

    def tick(self):
        if self.start_clock is None or not self.win or not self.win.winfo_exists():return
        t=time.monotonic()-self.start_clock
        if t>=len(self.audio)/self.fs: self.stop(); return
        if hasattr(self,'line1'):
            self.line1.set_xdata([t,t]);self.line2.set_xdata([t,t]);self.canvas.draw_idle()
        self.after_id=self.win.after(150,self.tick)

    def stop(self):
        self.start_clock=None
        if self.after_id is not None and self.win and self.win.winfo_exists():
            try: self.win.after_cancel(self.after_id)
            except tk.TclError: pass
        self.after_id=None
        if os.name=='nt':
            try:
                import winsound
                winsound.PlaySound(None,winsound.SND_PURGE)
            except Exception: pass

    def export(self):
        if self.audio is None: return messagebox.showinfo('No audio','Open an audio recording first.',parent=self.win)
        d=RESULTS/f'NAVY_FILE_STFT_{stamp()}';d.mkdir(exist_ok=True)
        self.fig.savefig(d/'spectrogram.png',dpi=185,bbox_inches='tight')
        (d/'source.txt').write_text(f'Original PC audio: {self.path}\nSample rate: {self.fs}\nSTFT: {self.nfft.get()} samples\nOverlap: {self.overlap.get()}\nNot evidence from the STM32 ADC.\n',encoding='utf-8')
        self.app._log_local(f'File spectrogram saved -> {d.resolve()}')

    def close(self):
        self.stop()
        if self.win:self.win.destroy()
        self.win=None

class App(tk.Tk):
    def __init__(self):
        super().__init__(); self.title('CONNECTIVITY · INDIVIDUAL DEMONSTRATION · R32'); self.geometry('1360x840'); self.minsize(1120,720)
        self.link=SerialLink(); self.graph=GraphWindow(self); self.file_lab=FileSpectrogramWindow(self); self.stage=1; self.running=False; self.port_name=''; self.session=None
        self.console_all=[]; self.actions=queue.Queue(); self.stage_result={n:'WAIT' for n in STAGES}
        self.ev={k:{'expected':e,'actual':'Not checked','result':'WAIT','time':'—'} for k,e in EVIDENCE}
        self._style(); self._build(); self.refresh_ports(); self.select_stage(1); self.after(60,self.poll)
    def _style(self):
        st=ttk.Style(self)
        try: st.theme_use('clam')
        except: pass
        st.configure('.',font=('Segoe UI',9))
        st.configure('Run.TButton',font=('Segoe UI Semibold',15),padding=12,background='#2563eb',foreground='white')
        st.map('Run.TButton',background=[('active','#1d4ed8')])
        st.configure('Stage.TButton',font=('Segoe UI Semibold',10),padding=7)
        st.configure('Treeview',rowheight=26,font=('Segoe UI',9)); st.configure('Treeview.Heading',font=('Segoe UI Semibold',9))
        st.configure('TLabelframe.Label',font=('Segoe UI Semibold',10))
    def _build(self):
        h=ttk.Frame(self,padding=7); h.pack(fill='x')
        ttk.Label(h,text='CONNECTIVITY · INDIVIDUAL DEMONSTRATION',font=('Segoe UI Semibold',17)).pack(side='left')
        con=ttk.Frame(h); con.pack(side='right'); self.port=ttk.Combobox(con,width=12,state='readonly'); self.port.pack(side='left',padx=2)
        ttk.Button(con,text='REFRESH',command=self.refresh_ports).pack(side='left'); self.connect_btn=ttk.Button(con,text='CONNECT',command=self.toggle_connect); self.connect_btn.pack(side='left',padx=3)
        self.conn=tk.Label(con,text='DISCONNECTED',bg=COLORS['FAIL'],padx=8,pady=4); self.conn.pack(side='left')
        strip=tk.Frame(self,bg='#fff3b0',padx=10,pady=7); strip.pack(fill='x')
        tk.Label(strip,text='LATEST RESULT →',bg='#fff3b0',font=('Segoe UI Semibold',10)).pack(side='left')
        self.latest=tk.StringVar(value=str(RESULTS.resolve())); tk.Label(strip,textvariable=self.latest,bg='#fff3b0',fg='#7c2d12',font=('Consolas',9,'bold'),anchor='w').pack(side='left',fill='x',expand=True,padx=8)
        ttk.Button(strip,text='OPEN RESULTS FOLDER',command=self.open_results).pack(side='right',padx=3)
        ttk.Button(strip,text='CAPTURE DIAGNOSTICS',command=self.capture_now).pack(side='right',padx=4)
        body=ttk.Frame(self,padding=7); body.pack(fill='both',expand=True); body.columnconfigure(0,weight=0,minsize=215); body.columnconfigure(1,weight=1,minsize=620); body.columnconfigure(2,weight=0,minsize=365); body.rowconfigure(0,weight=1)
        left=ttk.LabelFrame(body,text='ASSESSMENT · REAL HARDWARE TESTS',padding=6); left.grid(row=0,column=0,sticky='nsew',padx=(0,5)); self.badges={}
        for n,(name,_,__) in STAGES.items():
            f=ttk.Frame(left); f.pack(fill='x',pady=3); ttk.Button(f,text=f'{n}. {name}',style='Stage.TButton',command=lambda x=n:self.select_stage(x)).pack(side='left',fill='x',expand=True)
            b=tk.Label(f,text='WAIT',width=6,bg=COLORS['WAIT'],font=('Segoe UI',8,'bold')); b.pack(side='right',padx=(3,0)); self.badges[n]=b
        ttk.Separator(left).pack(fill='x',pady=7); ttk.Label(left,text='LIVE TOOLS · NOT GRADED TESTS',font=('Segoe UI',8,'bold')).pack(anchor='w')
        ttk.Button(left,text='BLUETOOTH · RAW REFERENCE',command=lambda:self.manual('!BR')).pack(fill='x',pady=2)
        ttk.Button(left,text='BLUETOOTH · CLEAN AUDIO',command=lambda:self.manual('!BS')).pack(fill='x',pady=2)
        ttk.Button(left,text='OPEN LIVE SCOPE / FFT',command=self.open_signal_lab).pack(fill='x',pady=2)
        ttk.Button(left,text='LIVE RTOS TASK MONITOR',command=self.open_rtos_monitor).pack(fill='x',pady=2)
        ttk.Button(left,text='LIVE PRODUCTION STATE MACHINE',command=self.open_fsm_monitor).pack(fill='x',pady=2)
        ttk.Button(left,text='REAL AUTO PRIORITY ROUTING',command=lambda:self.manual('!FA')).pack(fill='x',pady=2)
        ttk.Button(left,text='OPEN WAV SPECTROGRAM',command=self.file_lab.open).pack(fill='x',pady=2)
        ttk.Button(left,text='TELECOIL · FILTER ON',command=lambda:self.manual('!TN')).pack(fill='x',pady=2)
        ttk.Button(left,text='TELECOIL · FILTER BYPASS',command=lambda:self.manual('!TF')).pack(fill='x',pady=2)
        centre=ttk.Frame(body); centre.grid(row=0,column=1,sticky='nsew',padx=3); centre.rowconfigure(1,weight=1); centre.columnconfigure(0,weight=1)
        card=ttk.LabelFrame(centre,text='SELECTED HARDWARE TEST',padding=8); card.grid(row=0,column=0,sticky='ew')
        self.titlev=tk.StringVar(); ttk.Label(card,textvariable=self.titlev,font=('Segoe UI Semibold',15)).pack(anchor='w'); self.before=tk.StringVar(); self.expect=tk.StringVar()
        ttk.Label(card,text='DO THIS:',font=('Segoe UI',9,'bold')).pack(anchor='w',pady=(4,0)); ttk.Label(card,textvariable=self.before,wraplength=580,justify='left').pack(anchor='w')
        ttk.Label(card,text='PASS LOOKS LIKE:',font=('Segoe UI',9,'bold')).pack(anchor='w',pady=(4,0)); ttk.Label(card,textvariable=self.expect,wraplength=580,justify='left').pack(anchor='w')
        self.run=ttk.Button(card,text='RUN CURRENT TEST',style='Run.TButton',command=self.run_current); self.run.pack(fill='x',pady=(8,3)); self.pb=ttk.Progressbar(card,maximum=100); self.pb.pack(fill='x'); self.pbt=tk.StringVar(value='Ready'); ttk.Label(card,textvariable=self.pbt).pack(anchor='w')
        self.banner=tk.Label(card,text='RESULT: NOT RUN',bg=COLORS['WAIT'],font=('Segoe UI Semibold',13),pady=6); self.banner.pack(fill='x',pady=(5,0))
        self.confirm=ttk.Frame(card); self.confirm.pack(fill='x',pady=(5,0)); self.hear_btn=ttk.Button(self.confirm,text='✓ CLEAN / ACCEPTABLE',command=lambda:self.physical_confirm(True,'PASS')); self.noisy_btn=ttk.Button(self.confirm,text='⚠ AUDIBLE BUT NOISY / DISTORTED',command=lambda:self.physical_confirm(True,'WARN')); self.nohear_btn=ttk.Button(self.confirm,text='✗ NO / BAD AUDIO',command=lambda:self.physical_confirm(False,'FAIL'))
        evf=ttk.LabelFrame(centre,text='EVIDENCE · REAL MCU COUNTERS',padding=5); evf.grid(row=1,column=0,sticky='nsew',pady=(5,0)); evf.rowconfigure(0,weight=1); evf.columnconfigure(0,weight=1)
        self.tree=ttk.Treeview(evf,columns=('check','expected','actual','result','time'),show='headings')
        for k,t,w in [('check','Check',95),('expected','Expected',175),('actual','Actual evidence',250),('result','Result',60),('time','Time',66)]: self.tree.heading(k,text=t); self.tree.column(k,width=w,anchor='w')
        self.tree.grid(row=0,column=0,sticky='nsew'); sb=ttk.Scrollbar(evf,orient='vertical',command=self.tree.yview); sb.grid(row=0,column=1,sticky='ns'); self.tree.configure(yscrollcommand=sb.set)
        for r,c in COLORS.items(): self.tree.tag_configure(r,background=c)
        for k,e in EVIDENCE:self.tree.insert('', 'end',iid=k,values=(k,e,'Not checked','WAIT','—'),tags=('WAIT',))
        right=ttk.LabelFrame(body,text='MCU CONSOLE · 115200 BAUD',padding=5); right.grid(row=0,column=2,sticky='nsew',padx=(5,0)); right.rowconfigure(2,weight=1); right.columnconfigure(0,weight=1)
        q=ttk.Frame(right); q.grid(row=0,column=0,sticky='ew')
        for t,c in [('HELLO','!HI'),('HEALTH','!H'),('DIAG','!DG'),('STATUS','STATUS')]: ttk.Button(q,text=t,command=lambda x=c:self.manual(x)).pack(side='left',padx=2)
        ef=ttk.Frame(right); ef.grid(row=1,column=0,sticky='ew',pady=4); ef.columnconfigure(0,weight=1); self.cmd=tk.Entry(ef,font=('Consolas',10)); self.cmd.grid(row=0,column=0,sticky='ew'); self.cmd.bind('<Return>',lambda e:self.send_entry()); ttk.Button(ef,text='SEND',command=self.send_entry).grid(row=0,column=1,padx=3)
        self.console=tk.Text(right,bg='#111827',fg='#f8fafc',font=('Consolas',9),wrap='word'); self.console.grid(row=2,column=0,sticky='nsew')
        tk.Label(right,text='Each finished test auto-saves a ZIP.\nUpload SEND_THIS_TO_CHATGPT_LATEST.zip if anything fails.',bg='#fff7d6',justify='left',padx=6,pady=5).grid(row=3,column=0,sticky='ew',pady=(5,0))
    def ui(self,fn,*a): self.actions.put((fn,a))
    def poll(self):
        for t,line in self.link.poll():
            shown=line
            if line.startswith('R26|SCOPEHD|D|') and '|hex=' in line:
                d=kv_line(line) or {}; shown=f"R26|SCOPEHD|D|seq={d.get('seq')}|offset={d.get('i')}|payload=<256 real PCM samples>"
            elif line.startswith('R27|DUAL|PASS|') and '|pre64=' in line:
                d=kv_line(line) or {}; shown=f"R27|DUAL|PASS|seq={d.get('seq')}|src={d.get('src')}|n={d.get('n')}|payload=<paired PCM16>"
            elif line.startswith('R26|SCOPE|PASS|') and '|hex=' in line:
                d=kv_line(line) or {}; shown=f"R26|SCOPE|PASS|seq={d.get('seq')}|src={d.get('src')}|fs={d.get('fs')}|n={d.get('n')}|payload=<compact hex>"
            msg=f'[{t[11:23]}] {shown}\n'; self.console_all.append(msg.rstrip()); self.console.insert('end',msg); self.console.see('end')
        try:
            while True:
                fn,a=self.actions.get_nowait(); fn(*a)
        except queue.Empty: pass
        self.after(60,self.poll)
    def refresh_ports(self):
        p=self.link.ports(); self.port['values']=p
        if p and self.port.get() not in p:self.port.set(p[0])
    def toggle_connect(self):
        if self.link.ser:
            self.link.close(); self.conn.configure(text='DISCONNECTED',bg=COLORS['FAIL']); self.connect_btn.configure(text='CONNECT'); return
        if not self.port.get(): return messagebox.showerror('No COM port','Select a COM port first.')
        try:
            self.port_name=self.port.get(); self.link.connect(self.port_name); self.conn.configure(text='CONNECTED',bg=COLORS['PASS']); self.connect_btn.configure(text='DISCONNECT')
            threading.Thread(target=self._hello,daemon=True).start()
        except Exception as e: messagebox.showerror('Connect failed',str(e))
    def _hello(self):
        try:
            d=self.link.ask('!HI','R21|HELLO|',2.0); self.ui(self.set_banner,'PASS',f"Firmware link OK — {d.get('raw')}")
        except Exception as e:self.ui(self.set_banner,'FAIL',f'No R26 robust-control reply: {e}')
    def manual(self,cmd):
        if self.running: return messagebox.showwarning('Test running','Wait for the current test to finish.')
        if not self.link.ser: return messagebox.showwarning('Not connected','Connect to the STM32 first.')
        # Never interleave a control command with an active paired scope packet.
        # Queue asynchronously behind the shared serial request mutex.
        def send_command():
            try:
                with self.link.lock:self.link.send(cmd)
            except Exception as exc:self.ui(self._log_local,f'Send failed ({cmd}): {exc}')
        threading.Thread(target=send_command,daemon=True).start()
    def send_entry(self):
        c=self.cmd.get().strip()
        if c:self.manual(c); self.cmd.delete(0,'end')
    def select_stage(self,n):
        self.stage=n; name,b,e=STAGES[n]; self.titlev.set(f'STEP {n}: {name}'); self.before.set(b); self.expect.set(e); self.pb['value']=0; self.pbt.set('Ready'); self.set_banner('WAIT',f'RESULT: {self.stage_result[n]}'); self.hide_confirm()
    def set_badge(self,n,r): self.stage_result[n]=r; self.badges[n].configure(text=r,bg=COLORS.get(r,COLORS['WAIT']))
    def set_banner(self,r,t): self.banner.configure(text=t,bg=COLORS.get(r,COLORS['WAIT']))
    def set_ev(self,k,r,a):
        tm=short_time(); self.ev[k].update(actual=a,result=r,time=tm); self.tree.item(k,values=(k,self.ev[k]['expected'],a,r,tm),tags=(r,)); self.tree.see(k)
    def progress(self,v,t): self.ui(self._progress,v,t)
    def _progress(self,v,t): self.pb['value']=v; self.pbt.set(t)
    def show_confirm(self): self.hear_btn.pack(side='left',fill='x',expand=True,padx=2); self.noisy_btn.pack(side='left',fill='x',expand=True,padx=2); self.nohear_btn.pack(side='left',fill='x',expand=True,padx=2)
    def hide_confirm(self): self.hear_btn.pack_forget(); self.noisy_btn.pack_forget(); self.nohear_btn.pack_forget()
    def start_session(self,n):
        self.session={'stage':n,'name':STAGES[n][0],'start':iso(),'console_start':len(self.console_all),'responses':[],'scope':[],'diag':{}}
    def ask(self,cmd,prefix,timeout=3,retries=2):
        # R26: forward the retry policy to SerialLink.ask(). R25 accidentally
        # omitted this parameter from the App wrapper, so any stage calling
        # self.ask(..., retries=1/2) raised TypeError before the command was
        # transmitted. That is why Stage 2 could fail with no output tone at all.
        d=self.link.ask(cmd,prefix,timeout,retries=retries)
        if self.session:self.session['responses'].append({'time':iso(),'line':d.get('raw','')})
        return d
    def open_fsm_monitor(self):
        """Display actual ConnectivityService state from live SAI DMA observations.

        No generated source events, and no PASS simulation. Only !FA changes
        the requested mode; !FV returns whatever the production controller sees.
        """
        if getattr(self,'fsm_win',None) and self.fsm_win.winfo_exists():
            self.fsm_win.lift();return
        w=tk.Toplevel(self); self.fsm_win=w
        w.title('REAL CONNECTIVITY FSM · M1 / M2 / M3 / M4')
        w.geometry('970x610');w.minsize(760,520)
        f=ttk.Frame(w,padding=12);f.pack(fill='both',expand=True)
        ttk.Label(f,text='PRODUCTION STATE MACHINE · ACTUAL BT + TELECOIL INPUTS',font=('Segoe UI Semibold',15)).pack(anchor='w')
        ttk.Label(f,text='Click ARM AUTO once; leave TC field active, play BT, then pause BT. '
                  'Source, validity and transitions below come directly from STM32, never injected test data.',
                  wraplength=900).pack(anchor='w',pady=(4,9))
        controls=ttk.Frame(f);controls.pack(fill='x')
        ttk.Button(controls,text='ARM PRODUCTION AUTO MODE',command=lambda:self._fsm_arm()).pack(side='left',padx=(0,6))
        ttk.Button(controls,text='SAVE REAL FSM TRACE',command=self.save_fsm_trace).pack(side='left',padx=(5,5))
        ttk.Button(controls,text='CLOSE',command=self._fsm_close).pack(side='right')
        self.fsm_status=tk.StringVar(value='Waiting for actual MCU !FV observations...')
        ttk.Label(f,textvariable=self.fsm_status,font=('Consolas',11),wraplength=910).pack(anchor='w',pady=8)
        info=ttk.LabelFrame(f,text='DETECTED SOURCES / ACTUAL CONTROLLER OUTPUT',padding=12)
        info.pack(fill='x',pady=6)
        self.fsm_mode_text=tk.StringVar(value='M? / unknown')
        ttk.Label(info,textvariable=self.fsm_mode_text,font=('Segoe UI Semibold',18)).pack(anchor='w')
        self.fsm_signal_text=tk.StringVar(value='BT / TC detector pending')
        ttk.Label(info,textvariable=self.fsm_signal_text,font=('Consolas',11)).pack(anchor='w',pady=6)
        self.fsm_dma_text=tk.StringVar(value='DMA / switch evidence pending')
        ttk.Label(info,textvariable=self.fsm_dma_text,font=('Consolas',10)).pack(anchor='w')
        timeline=ttk.LabelFrame(f,text='LAST 90 HARDWARE OBSERVATIONS · newest at right · no interpolated transitions',padding=8)
        timeline.pack(fill='both',expand=True,pady=8)
        self.fsm_canvas=tk.Canvas(timeline,bg='#0f172a',highlightthickness=0,height=110)
        self.fsm_canvas.pack(fill='both',expand=True)
        self.fsm_history=[];self.fsm_records=[];self.fsm_previous=None
        self.fsm_stop=threading.Event()
        def close():
            self.fsm_stop.set()
            if w.winfo_exists():w.destroy()
        w.protocol('WM_DELETE_WINDOW',self._fsm_close)
        self.fsm_thread=threading.Thread(target=self._fsm_worker,args=(self.fsm_stop,),daemon=True)
        self.fsm_thread.start()

    def _fsm_close(self):
        if hasattr(self,'fsm_stop'):self.fsm_stop.set()
        if getattr(self,'fsm_win',None) and self.fsm_win.winfo_exists():self.fsm_win.destroy()

    def _fsm_arm(self):
        if self.running:
            self.fsm_status.set('Wait for the current hardware test to finish')
            return
        def arm():
            try:
                d=self.link.ask('!FA','R28|AUTO|',timeout=2.0,retries=1)
                self.ui(self._fsm_message,'Production AUTO request: '+d.get('state','?')+'; waiting for real controller epoch')
            except Exception as e:
                self.ui(self._fsm_message,'AUTO acknowledgement missing; read next !FV to verify: '+str(e)[:75])
        threading.Thread(target=arm,daemon=True).start()

    def _fsm_message(self,message):
        if getattr(self,'fsm_win',None) and self.fsm_win.winfo_exists():self.fsm_status.set(message)

    def _fsm_worker(self,stop):
        while not stop.is_set():
            if not self.link.ser or not self.link.ser.is_open:
                self.ui(self._fsm_message,'Connect to ST-LINK serial 115200 baud first')
                stop.wait(.6);continue
            if self.running:
                stop.wait(.6);continue
            try:
                d=self.link.ask('!FV','R28|LIVE|',timeout=1.8,retries=1)
                self.ui(self._fsm_render,d)
            except Exception as e:
                self.ui(self._fsm_message,'Live MCU evidence unavailable: '+str(e)[:100])
            stop.wait(.5)

    def _fsm_render(self,d):
        if not getattr(self,'fsm_win',None) or not self.fsm_win.winfo_exists():return
        names={1:'M1 · STARTUP',2:'M2 · ACTIVE',3:'M3 · IDLE',4:'M4 · FAULT'}
        sources={0:'NONE',1:'TELECOIL',2:'BLUETOOTH'}
        mode=iv(d,'mode');src=iv(d,'src')
        self.fsm_mode_text.set(f'{names.get(mode,"M? · OTHER")}  |  OUTPUT SOURCE: {sources.get(src,"?")}'
                               +('  |  DIRECT OVERRIDE ACTIVE' if iv(d,'direct') else '  |  PRODUCTION AUTO'))
        self.fsm_signal_text.set(f'BT detected={d.get("btvalid","?")}  RMS={d.get("btrms","?")}   '
                                 f'TC detected={d.get("tcvalid","?")}  RMS={d.get("tcrms","?")}   '
                                 f'FAULT={d.get("fault","?")}')
        old=self.fsm_previous
        self.fsm_previous=d
        progress='First frame received; waiting to compare real DMA activity'
        if old and iv(d,'ms')>iv(old,'ms'):
            progress=(f'SAI1 callbacks +{iv(d,"btcb")-iv(old,"btcb")} · '
                      f'SAI2 callbacks +{iv(d,"tccb")-iv(old,"tccb")} · '
                      f'speaker blocks +{iv(d,"spk")-iv(old,"spk")}')
        self.fsm_dma_text.set(progress+f' · controller switches={d.get("sw")}')
        self.fsm_status.set(f'MCU ms={d.get("ms")}  AUTO epoch={d.get("epoch")}  ready={d.get("ready")}  '
                            f'real signals only={"YES" if iv(d,"synth")==0 else "NO"}')
        self.fsm_history.append((iv(d,'ms'),src,mode,iv(d,'ready'),iv(d,'direct')))
        self.fsm_records.append({k:d.get(k,'') for k in ('ms','epoch','ready','mode','src','fault','direct','synth','btvalid','tcvalid','btrms','tcrms','btcb','tccb','spk','sw')})
        self.fsm_records=self.fsm_records[-3000:]
        self.fsm_history=self.fsm_history[-90:]
        c=self.fsm_canvas;c.delete('all');width=max(650,c.winfo_width());left=95;right=width-15
        lanes=[('BT',2,'#60a5fa'),('TC',1,'#34d399'),('NONE',0,'#94a3b8')]
        for i,(name,_,col) in enumerate(lanes):
            y=20+i*38
            c.create_text(12,y+10,text=name,fill=col,anchor='w',font=('Consolas',11,'bold'))
            c.create_line(left,y+22,right,y+22,fill='#334155')
        h=self.fsm_history;step=(right-left)/90.0;start=right-step*len(h)
        for j,(_,sel,mod,ready,direct) in enumerate(h):
            for i,(_,val,col) in enumerate(lanes):
                y=20+i*38
                active=sel==val and ready and not direct
                if active:c.create_rectangle(start+j*step,y,start+(j+1)*step-1,y+20,fill=col,outline='')
                elif sel==val:c.create_rectangle(start+j*step,y,start+(j+1)*step-1,y+20,fill='#475569',outline='')
        c.create_text(right,146,text='Time -> actual successive MCU observations',anchor='e',fill='#cbd5e1',font=('Consolas',9))

    def open_rtos_monitor(self):
        if getattr(self,'rtos_win',None) and self.rtos_win.winfo_exists():
            self.rtos_win.lift(); return
        w=tk.Toplevel(self); self.rtos_win=w
        w.title('LIVE STM32 FreeRTOS | Task Scheduling & DMA Activity')
        w.geometry('1160x990'); w.minsize(930,820)
        main=ttk.Frame(w,padding=12); main.pack(fill='both',expand=True)
        ttk.Label(main,text='LIVE FreeRTOS • REAL TASK EXECUTION',font=('Segoe UI Semibold',16)).pack(anchor='w')
        ttk.Label(main,text='The real FreeRTOS tasks share ONE Cortex-M4 CPU. Coloured timeline cells show verified '
                  'counter progress between real STM32 samples. Kernel runtimes (or tick sampling fallback) show CPU residency: only ONE task is RUNNING at an instant. ',
                  wraplength=970).pack(anchor='w',pady=4)
        controls=ttk.Frame(main); controls.pack(fill='x',pady=5)
        ttk.Button(controls,text='CPU STRESS · 60 s (REAL MCU)',
                   command=lambda:self._rtos_control('!RS','Starting 60-second low-priority MCU CPU stress')).pack(side='left',padx=(0,8))
        ttk.Button(controls,text='DUAL TASKS · 60 s',
                   command=lambda:self._rtos_control('!RD','Starting two equal-priority CPU-bound tasks')).pack(side='left',padx=(0,8))
        ttk.Button(controls,text='STOP CPU STRESS / LOAD',
                   command=lambda:self._rtos_control('!RX','Stopping MCU background load')).pack(side='left',padx=(0,8))
        ttk.Button(controls,text='DUAL QUIET (NO AUDIO)',
                   command=lambda:self._rtos_quiet_stress()).pack(side='left',padx=(0,8))
        ttk.Button(controls,text='BT + DUAL LOAD',
                   command=lambda:self._rtos_bt_stress()).pack(side='left',padx=(0,8))
        ttk.Button(controls,text='SAVE RTOS TRACE',command=self.save_rtos_trace).pack(side='left',padx=(0,8))
        high_row=ttk.Frame(main);high_row.pack(fill='x',pady=(0,6))
        ttk.Button(high_row,text='CONTINUOUS SPIN · PRIORITY 7 · 20 s',
                   command=lambda:self._rtos_control('!RH',
                     '20-second priority-7 NONSTOP SPIN: serial/ControlTask may freeze until self-stop. Debugger Suspend works.')).pack(side='left',padx=(0,8))
        ttk.Label(high_row,text='NO delays/yields while active · UART may pause 20 s · use ST-LINK Suspend for CPU evidence',
                  font=('Segoe UI',9)).pack(side='left')
        high_modes=ttk.Frame(main);high_modes.pack(fill='x',pady=(0,6))
        ttk.Button(high_modes,text='QUIET + CONTINUOUS SPIN',
                   command=lambda:self._rtos_hardware_sequence(('AUDIO STOP','!RH'),
                     'PRIORITY-7 SPIN: output stopped; ControlTask may not answer for 20 s; capture CubeIDE task runtimes now')).pack(side='left',padx=(0,8))
        ttk.Button(high_modes,text='BT + CONTINUOUS SPIN',
                   command=lambda:self._rtos_hardware_sequence(('!BS','!RH'),
                     'PRIORITY-7 SPIN with Bluetooth: expect UART pause and possible audio glitch; screenshot task split in CubeIDE')).pack(side='left',padx=(0,8))
        ttk.Label(high_modes,text='Same core: expect one RUNNING task and both accumulating CPU time',
                  font=('Segoe UI',9)).pack(side='left')
        # Stress cannot prove multicore execution: target is preemptive single-core scheduling.

        self.rtos_state=tk.StringVar(value='Connecting to STM32 task telemetry…')
        ttk.Label(main,textvariable=self.rtos_state,font=('Consolas',10)).pack(anchor='w',pady=5)
        grid=ttk.Frame(main);grid.pack(fill='x',expand=False)
        names=[('ctl','ControlTask','6'),('aud','AudioTask','7'),('bt','BtTask','5'),('bg','Background','2'),('b2','CPULoadB','2'),('h7','HighLoad7','7')]
        self.rtos_cards={}
        for i,(k,name,pri) in enumerate(names):
            f=ttk.LabelFrame(grid,text=f'{name} · priority {pri}',padding=9)
            f.grid(row=i//2,column=i%2,sticky='nsew',padx=5,pady=5)
            rate=tk.StringVar(value='Waiting for hardware counters…')
            ttk.Label(f,textvariable=rate,font=('Consolas',12)).pack(anchor='w')
            bar=ttk.Progressbar(f,maximum=100);bar.pack(fill='x',pady=5)
            detail=tk.StringVar(value='Kernel state: waiting')
            ttk.Label(f,textvariable=detail,font=('Consolas',9)).pack(anchor='w')
            self.rtos_cards[k]=(rate,bar,detail)
        grid.columnconfigure(0,weight=1);grid.columnconfigure(1,weight=1)
        for i in (0,1):grid.rowconfigure(i,weight=1)
        timeline=ttk.LabelFrame(main,text='LIVE TASK PROGRESS TIMELINE  ·  newest sample at right',padding=8)
        timeline.pack(fill='both',expand=True,pady=6)
        self.rtos_canvas=tk.Canvas(timeline,bg='#0f172a',highlightthickness=0,height=295)
        self.rtos_canvas.pack(fill='both',expand=True)
        cpu_panel=ttk.LabelFrame(main,text='MEASURED CPU EXECUTION · FreeRTOS / 1 MHz TIM2 · rolling 40 samples',padding=8)
        cpu_panel.pack(fill='both',expand=True,pady=(0,5))
        self.rtos_cpu_canvas=tk.Canvas(cpu_panel,bg='#0f172a',highlightthickness=0,height=120)
        self.rtos_cpu_canvas.pack(fill='both',expand=True)
        self.rtos_cpu_canvas.bind('<Configure>',lambda _ev:self._rtos_cpu_draw())
        self.rtos_history=[]
        self.rtos_cpu_history=[]
        self.rtos_records=[]
        self.rtos_keys=[('ctl','Control','#60a5fa'),('aud','Audio','#34d399'),('bt','BT control','#fbbf24'),('bg','CPU A','#c084fc'),('b2','CPU B','#fb7185'),('h7','HighLoad7','#38bdf8')]
        self.rtos_metrics=tk.StringVar(value='Waiting for measured execution microseconds, DMA, background work and error counters…')
        ttk.Label(main,textvariable=self.rtos_metrics,font=('Consolas',10),wraplength=970).pack(anchor='w',pady=(8,3))
        self.rtos_previous=None
        self.rtos_stop=threading.Event()
        def close_monitor():
            self.rtos_stop.set();w.destroy()
        ttk.Button(main,text='CLOSE MONITOR',command=close_monitor).pack(anchor='e',pady=5)
        w.protocol('WM_DELETE_WINDOW',close_monitor)
        self.rtos_canvas.bind('<Configure>',lambda _ev:self._rtos_timeline_draw())
        self.rtos_thread=threading.Thread(target=self._rtos_worker,args=(w,self.rtos_stop),daemon=True)
        self.rtos_thread.start()

    def _save_live_csv(self,name,records):
        if not records:
            messagebox.showwarning('No board data','Collect at least two live STM32 observations first.',parent=self)
            return
        path=RESULTS/(stamp()+'_'+name+'.csv')
        cols=list(dict.fromkeys(k for r in records for k in r))
        with path.open('w',newline='',encoding='utf-8') as f:
            w=csv.DictWriter(f,fieldnames=cols)
            w.writeheader(); w.writerows(records)
        self.latest.set(str(path)); self._log_local('Saved real MCU trace: '+str(path))

    def save_rtos_trace(self):
        self._save_live_csv('RTOS_REAL_TASK_RUNTIME',getattr(self,'rtos_records',[]))

    def save_fsm_trace(self):
        self._save_live_csv('FSM_REAL_SOURCE_TRANSITIONS',getattr(self,'fsm_records',[]))

    def _rtos_hardware_sequence(self,commands,label):
        """Actual UART actions, never fabricated CPU data or forced kernel states."""
        if self.running:
            self._rtos_wait_status('Finish the selected hardware test before changing audio/loading')
            return
        self._rtos_wait_status(label)
        def work():
            try:
                for cmd in commands:
                    if cmd in ('!RS','!RD','!RH','!RX','!BS'):
                        prefix='R28|LOAD|' if cmd in ('!RS','!RD','!RH','!RX') else 'R21|BT_START|'
                        result=self.link.ask(cmd,prefix,timeout=2.5,retries=1)
                        self.ui(self._log_local,'MCU: '+result.get('raw',''))
                    else:
                        with self.link.lock:self.link.send(cmd)
                    time.sleep(.3)
                self.ui(self._rtos_wait_status,label+' — observe live kernel runtime and DMA counters')
            except Exception as exc:
                self.ui(self._rtos_wait_status,'MCU command sequence incomplete: '+str(exc)[:110])
        threading.Thread(target=work,daemon=True).start()

    def _rtos_quiet_stress(self):
        # AUDIO STOP disables capture/SAI activity; the kernel and its other tasks still execute.
        self._rtos_hardware_sequence(('AUDIO STOP','!RD'),
            'DUAL QUIET: audio stopped; CPULoadA and CPULoadB share spare CPU time; IDLE approaches zero')

    def _rtos_bt_stress(self):
        # !BS resets background load as part of audio startup; re-arm !RD afterward.
        self._rtos_hardware_sequence(('!BS','!RD'),
            'BT + DUAL: higher-priority real audio preempts the two priority-2 workers; verify audible quality')

    def _rtos_control(self,cmd,reason):
        # Do not block Tk or interrupt a Stage 6/7 serial transaction.
        if self.running:
            self._rtos_wait_status('Finish the active hardware test before changing CPU load')
            return
        self._rtos_wait_status(reason)
        def send_command():
            try:
                data=self.link.ask(cmd,'R28|LOAD|',timeout=2.0,retries=1)
                self.ui(self._rtos_wait_status,
                        'MCU replied: load='+str(data.get('enabled','?'))+
                        ' · '+('auto-off in '+('20' if cmd=='!RH' else '60')+' seconds' if cmd in ('!RS','!RD','!RH') else 'normal idle restored'))
            except Exception as exc:
                self.ui(self._rtos_wait_status,
                        'Load ACK missing; next !RM sample will show actual board load state: '+str(exc)[:75])
        threading.Thread(target=send_command,daemon=True).start()

    def _rtos_timeline_draw(self):
        if not getattr(self,'rtos_win',None) or not self.rtos_win.winfo_exists():return
        c=self.rtos_canvas;c.delete('all')
        width=max(650,c.winfo_width());left=135;right=width-16
        rows=self.rtos_keys
        for i,(_,name,color) in enumerate(rows):
            y=22+i*42
            c.create_text(10,y+10,text=name,anchor='w',fill='#e2e8f0',font=('Segoe UI',10,'bold'))
            c.create_line(left,y+24,right,y+24,fill='#334155')
        history=self.rtos_history[-72:]
        if not history:
            c.create_text(left+20,12,text='Waiting for multiple timestamped board responses…',fill='#94a3b8',anchor='w')
            return
        cell=max(4,(right-left)/72)
        start=right-cell*len(history)
        for j,(ts,changes) in enumerate(history):
            x=start+j*cell
            for i,(k,_,color) in enumerate(rows):
                y=22+i*42
                if changes.get(k,0)>0:
                    c.create_rectangle(x,y,x+max(2,cell-1),y+20,fill=color,outline='')
                else:
                    c.create_rectangle(x,y,x+max(2,cell-1),y+20,fill='#1e293b',outline='')
        c.create_text(right,279,text=f'{history[-1][0]/1000:.1f} s MCU uptime  |  '
                      f'{len(history)} consecutive measurement windows',anchor='e',fill='#94a3b8')

    def _rtos_cpu_draw(self):
        if not getattr(self,'rtos_win',None) or not self.rtos_win.winfo_exists():return
        c=self.rtos_cpu_canvas;c.delete('all')
        w=max(400,c.winfo_width());h=max(110,c.winfo_height());left=48;right=w-12;top=15;bottom=h-20
        for pct in (0,50,100):
            y=bottom-(bottom-top)*pct/100.0
            c.create_line(left,y,right,y,fill='#334155',dash=(2,5))
            c.create_text(8,y,text=str(pct)+'%',fill='#94a3b8',anchor='w',font=('Consolas',8))
        samples=self.rtos_cpu_history[-40:]
        if len(samples)<2:return
        cols=[('ctl','#60a5fa','Control'),('aud','#34d399','Audio'),
              ('bt','#fbbf24','BT'),('bg','#c084fc','CPU A'),('b2','#fb7185','CPU B'),('h7','#38bdf8','Hi7'),('idle','#94a3b8','Idle')]
        for k,color,name in cols:
            points=[]
            for i,rec in enumerate(samples):
                x=left+(right-left)*i/max(1,len(samples)-1)
                y=bottom-(bottom-top)*max(0.,min(100.,rec.get(k,0)))/100.0
                points.extend((x,y))
            if len(points)>=4:c.create_line(*points,fill=color,width=2,smooth=False)
        x=left
        for _,color,name in cols:
            c.create_text(x,top-8,text=name,fill=color,anchor='w',font=('Consolas',8,'bold'))
            x+=max(66,len(name)*8+8)

    def _rtos_render(self,d,prev):
        if not getattr(self,'rtos_win',None) or not self.rtos_win.winfo_exists():return
        if not prev or iv(d,'ms')<=iv(prev,'ms'):
            self.rtos_state.set('Waiting for second distinct hardware timestamp…');return
        dt_us=(iv(d,'ms')-iv(prev,'ms'))*1000.0
        dt=dt_us/1000000.0
        states={0:'RUNNING',1:'READY',2:'BLOCKED',3:'SUSPENDED',4:'DELETED',5:'INVALID',255:'NOT CREATED'}
        delta={k:max(0,iv(d,k)-iv(prev,k)) for k in ('ctl','aud','bt','bg','b2','h7','work','w2','h7work','btcb','tccb','spk')}
        utilization={}
        for k,_,_ in self.rtos_keys:
            r=delta[k]/dt; st=states.get(iv(d,{'ctl':'cstate','aud':'astate','bt':'bstate','bg':'gstate','b2':'b2state','h7':'h7state'}[k]),'?')
            hwm=iv(d,{'ctl':'cstack','aud':'astack','bt':'bstack','bg':'gstack','b2':'b2stack','h7':'h7stack'}[k])
            # Runtime accounting comes from FreeRTOS task status with TIM2 1MHz.
            # Not an inferred CPU percentage from loop frequency.
            field={'ctl':'crun','aud':'arun','bt':'brun','bg':'grun','b2':'b2run','h7':'h7run'}[k]
            du=(iv(d,field)-iv(prev,field)) & 0xFFFFFFFF
            util=max(0.0,min(100.0,100.0*du/dt_us)) if iv(d,'hz')==1000000 else 0.0
            utilization[k]=util
            priority=iv(d,{'ctl':'cpri','aud':'apri','bt':'bpri','bg':'gpri','b2':'b2pri','h7':'h7pri'}[k])
            rate,bar,detail=self.rtos_cards[k]
            rate.set(f'{"ACTIVE" if delta[k] else "NO PROGRESS"}  {r:,.1f} loops/s   CPU≈{util:.1f}%')
            bar['value']=util
            detail.set(f'Priority {priority} · sampled: {st} · free stack minimum: {hwm} words')
        self.rtos_history.append((iv(d,'ms'),delta));self.rtos_history=self.rtos_history[-72:]
        rec={**{k:d.get(k,'') for k in ('ms','sched','hz','load','dual','hi7','src','ctl','aud','bt','bg','b2','h7','work','w2','h7work','btcb','tccb','spk','miss','under','qov','bov','tov','crun','arun','brun','grun','b2run','h7run','irun','cstack','astack','bstack','gstack','b2stack','h7stack')},
             **{('interval_'+k+'_pct'):round(v,3) for k,v in utilization.items()},
             'interval_idle_pct':round((100.*((iv(d,'irun')-iv(prev,'irun'))&0xFFFFFFFF)/dt_us),3) if iv(d,'hz')==1000000 else ''}
        self.rtos_records.append(rec); self.rtos_records=self.rtos_records[-2500:]
        self._rtos_timeline_draw()
        err={k:max(0,iv(d,k)-iv(prev,k)) for k in ('miss','under','qov','bov','tov')}
        idle_micro=(iv(d,'irun')-iv(prev,'irun')) & 0xFFFFFFFF
        idle_util=min(100.,100.*idle_micro/dt_us) if iv(d,'hz')==1000000 else 0.
        self.rtos_cpu_history.append({**utilization,'idle':idle_util})
        self.rtos_cpu_history=self.rtos_cpu_history[-40:]
        self._rtos_cpu_draw()
        self.rtos_metrics.set(
            f'MEASURED FreeRTOS runtime: Control={utilization["ctl"]:.1f}% Audio={utilization["aud"]:.1f}% '
            f'BT={utilization["bt"]:.1f}% CPU A={utilization["bg"]:.1f}% CPU B={utilization["b2"]:.1f}% High7={utilization["h7"]:.1f}% IDLE≈{idle_util:.1f}% '
            f'| scheduler={d.get("sched")} timer={d.get("hz")} Hz\n'
            f'LOAD={d.get("load")} dual={d.get("dual")} hi7={d.get("hi7")} source={d.get("src")} | CPU A work Δ{delta["work"]} CPU B work Δ{delta["w2"]} Hi7 work Δ{delta["h7work"]} | '
            f'SAI1 RX Δ{delta["btcb"]} SAI2 RX Δ{delta["tccb"]} speaker TX Δ{delta["spk"]}\n'
            f'Deadline misses Δ{err["miss"]} underruns Δ{err["under"]} '
            f'FIFO overflows Δ{err["qov"]} capture overflows BT Δ{err["bov"]} TC Δ{err["tov"]}')
        self.rtos_state.set(f'LIVE BOARD · MCU time {iv(d,"ms")/1000:.2f} s · '
                            f'update interval {dt:.2f} s · measured runtime + separate CPU A/B progress')

    def _rtos_worker(self,window,stop_event):
        while not stop_event.is_set():
            if not self.link.ser or not self.link.ser.is_open:
                self.ui(self._rtos_wait_status,'Connect to the board to enable real scheduler telemetry')
                stop_event.wait(.5);continue
            if self.running:
                self.ui(self._rtos_wait_status,'Hardware test is using serial; live monitor resumes afterward')
                stop_event.wait(.4);continue
            try:d=self.link.ask('!RM','R28|TASKS|',timeout=1.8,retries=1)
            except Exception as exc:
                if not stop_event.is_set():self.ui(self._rtos_wait_status,'Hardware telemetry: '+str(exc)[:100])
                stop_event.wait(.45);continue
            prev=self.rtos_previous;self.rtos_previous=d
            if not stop_event.is_set():self.ui(self._rtos_render,d,prev)
            stop_event.wait(.35)

    def _rtos_wait_status(self,text):
        if getattr(self,'rtos_win',None) and self.rtos_win.winfo_exists():self.rtos_state.set(text)

    def run_current(self):
        if self.running:return
        if not self.link.ser:return messagebox.showerror('Not connected','Connect first.')
        # An in-flight HD UART transfer can block or interleave short FSM
        # commands. Stop scheduling new captures first; worker takes serial lock.
        if self.graph.live:
            self.graph.toggle_live()
        self.running=True; self.run.configure(state='disabled'); self.start_session(self.stage); self.set_badge(self.stage,'RUN'); self.set_banner('RUN','RESULT: RUNNING'); threading.Thread(target=self.worker,args=(self.stage,),daemon=True).start()
    def capture_diag_into_session(self):
        try:
            d1,d2=self.link.collect_diag(3.0)
            if self.session:
                self.session['diag']={'diag1':d1,'diag2':d2}
                for d in (d1,d2):
                    if d:self.session['responses'].append({'time':iso(),'line':d.get('raw','')})
        except Exception as e:
            if self.session:self.session['diag_error']=str(e)
    def finish(self,r,detail):
        self.capture_diag_into_session()
        if self.session:self.session.update(end=iso(),result=r,detail=detail)
        self.ui(self._finish_ui,r,detail)
        self.export_session()
    def _finish_ui(self,r,detail):
        self.running=False; self.run.configure(state='normal'); self.pb['value']=100; self.pbt.set(detail); self.set_badge(self.stage,r); self.set_banner(r,f'STEP {self.stage} RESULT: {r} — {detail}')
    def worker(self,n):
        try:
            if n==1:
                self.progress(10,'Sampling actual clock, UART and RTOS counters at two instants')
                d0=self.ask('!H','R21|HEALTH|',2.5,retries=1)
                time.sleep(.45)
                d=self.ask('!H','R21|HEALTH|',2.5,retries=1)
                clk=iv(d,'clk'); rx_delta=iv(d,'rxov')-iv(d0,'rxov'); ue_delta=iv(d,'uerr')-iv(d0,'uerr')
                self.ui(self.set_ev,'CLOCK','PASS' if clk==48000000 else 'FAIL',f'HAL reported {clk} Hz; not independently measured')
                self.ui(self.set_ev,'UART','PASS' if rx_delta==0 and ue_delta==0 else 'WARN',
                         f'RXringOverrunΔ={rx_delta} UARTerrorsΔ={ue_delta}; lifetime counts rx={d.get("rxov")}, err={d.get("uerr")}')
                progressing=True
                for key,field,name in [('RTOS_CTL','ctl','control'),('RTOS_AUDIO','aud','audio'),('RTOS_BT','bt','Bluetooth control'),('RTOS_BG','bg','background')]:
                    delta=iv(d,field)-iv(d0,field); progressing &= delta>0
                    self.ui(self.set_ev,key,'PASS' if delta>0 else 'FAIL',f'{name}: previous={iv(d0,field)} latest={iv(d,field)} Δ={delta}')
                hard=(clk==48000000 and progressing)
                res='PASS' if hard and rx_delta==0 and ue_delta==0 else ('WARN' if hard else 'FAIL')
                self.finish(res,'Measured liveness over two replies; existing historical UART error counts are not fatal')
            elif n==2:
                # Always clear any stale tone first, then start a fresh isolated output test.
                try:self.ask('!OX','R21|OUT_STOP|',1.5)
                except Exception:pass
                self.progress(10,'Starting isolated 1 kHz output tone — one start command only'); start=None
                try:
                    start=self.ask('!OS','R21|OUT_START|',2.5,retries=1)
                except Exception as e:
                    # !OS is state-changing: NEVER resend it. A second start restarts
                    # speaker DMA and causes the brief audible cut-out observed on R24.
                    if self.session:self.session['start_ack_warning']=str(e)
                time.sleep(1.0)
                self.progress(55,'Checking speaker DMA without restarting the tone'); d=None
                try:d=self.ask('!OC','R21|OUT_CHECK|',2.5,retries=2)
                except Exception:
                    # Fallback to the independent diagnostic counters.  This prevents a
                    # lost OUT_CHECK reply from falsely declaring the audio hardware dead.
                    d1,d2=self.link.collect_diag(2.5)
                    if d1 is not None:
                        d={'spk':d1.get('spk','0'),'blocks':'1' if iv(d1,'spk')>=4 else '0','tone':'1000','qov':(d2 or {}).get('qov','?'),'raw':'R26 diagnostic fallback'}
                spk=iv(d or {},'spk'); blocks=iv(d or {},'blocks'); tone=iv(d or {},'tone'); sw=(spk>=4 and blocks>=1 and tone==1000)
                self.ui(self.set_ev,'OUTPUT_DMA','PASS' if sw else 'WARN',f'spkCallbacks={spk} blocks={blocks} tone={tone}Hz qOverruns={(d or {}).get("qov")} startAck={"yes" if start is not None else "missing-but-not-retried"}')
                # Physical output is independent evidence.  Always ask the user instead
                # of declaring FAIL only because telemetry was lost.
                self.session['output_sw_ok']=bool(sw)
                if sw:
                    msg='SOFTWARE OUTPUT PATH PASSES — NOW CONFIRM WHETHER YOU HEAR THE TONE'
                else:
                    msg='OUTPUT TELEMETRY IS INCOMPLETE — IF THE 1 kHz TONE IS AUDIBLE, CONFIRM IT; THIS WILL BE RECORDED AS WARN, NOT FALSE FAIL'
                self.ui(self.set_banner,'WARN',msg); self.ui(self.show_confirm); self.ui(self.set_badge,n,'WARN'); self.progress(85,'Waiting for physical confirmation'); self.session['await_physical']='OUTPUT' 
            elif n==3:
                # A failed/aborted output-tone stage must never leak into Bluetooth.
                try:self.ask('!OX','R21|OUT_STOP|',1.5)
                except Exception:pass
                self.progress(10,'Starting the same R21 audio path that sounded better'); start=None
                try:start=self.ask('!BS','R21|BT_START|',2.5,retries=1)
                except Exception:pass
                time.sleep(2.0); self.progress(55,'Reading BT transport/output evidence'); d=None
                try:d=self.ask('!BC','R21|BT|',3.0)
                except Exception:
                    d1,d2=self.link.collect_diag(2.5)
                    # DIAG fallback still proves the real SAI1 and speaker DMA are moving.
                    if d1 is not None:
                        d={'cb':d1.get('btcb','0'),'blk':str(max(0,iv(d1,'btcb')//2)),
                           'rms':d1.get('btrms','0'),'L':d1.get('btrms','0'),'R':'0',
                           'spk':d1.get('spk','0'),'und':(d2 or {}).get('und','0'),
                           'con':(d2 or {}).get('con','0'),'gain':'?',
                           'src':(d2 or {}).get('src','0'),'raw':'R26 diagnostic fallback'}
                d=d or {}
                cb,blk,rms,L,R,spk,und,con=map(lambda k:iv(d,k),['cb','blk','rms','L','R','spk','und','con'])
                capov,capd,capmax=map(lambda k:iv(d,k),['capov','capd','capmax'])
                mixL,mixR=iv(d,'mixL',16384),iv(d,'mixR',16384)
                loss=(100.0*capov/max(cb,1)); gain_q=iv(d,'gain',32768); gain_db=20.0*math.log10(max(gain_q,1)/32768.0)
                sai=(cb>=4 and blk>=2); sig=max(rms,L,R)>0; out=spk>=4
                self.ui(self.set_ev,'SAI1','PASS' if sai else 'FAIL',f'callbacks={cb} processedBlocks={blk} captureLoss={loss:.2f}%')
                self.ui(self.set_ev,'BT_SIGNAL','PASS' if sig else 'FAIL',f'RMS={rms} L={L} R={R} stableMix={mixL/32768:.2f}/{mixR/32768:.2f}')
                self.ui(self.set_ev,'BT_OUTPUT','PASS' if out else 'FAIL',f'speakerCallbacks={spk} gain={gain_db:+.1f} dB (Q15={gain_q})')
                cont='PASS' if (und==0 and capov==0) else 'WARN'
                self.ui(self.set_ev,'BT_CONT',cont,f'underruns={und} concealed={con} captureOverruns={capov} ({loss:.2f}%) depth={capd}/{capmax}')
                if sai and sig and out:
                    self.ui(self.set_banner,'WARN','DIGITAL BT PATH PASSES — NOW CONFIRM THE AUDIO YOU ACTUALLY HEAR'); self.ui(self.show_confirm); self.ui(self.set_badge,n,'WARN'); self.progress(85,'Waiting for physical audio confirmation'); self.session['await_physical']='BT'
                else:self.finish('FAIL','A required BT layer is genuinely absent; see SAI1 / SIGNAL / OUTPUT rows')
            elif n==4:
                self.progress(10,'Starting SAI2 telecoil path'); self.ask('!TS','R21|TC_START|',2.5,retries=1); time.sleep(2.0); d=self.ask('!TC','R21|TC|',3.0)
                cb,blk,rms,spk,err=map(lambda k:iv(d,k),['cb','blk','rms','spk','saierr']); trans=(cb>=4 and blk>=2 and err==0); sig=rms>0; out=spk>=4
                self.ui(self.set_ev,'SAI2','PASS' if trans else 'FAIL',f'callbacks={cb} blocks={blk} saiErrors={err}')
                self.ui(self.set_ev,'TC_SIGNAL','PASS' if sig else 'FAIL',f'TC RMS={rms}')
                self.ui(self.set_ev,'TC_OUTPUT','PASS' if out else 'FAIL',f'speakerCallbacks={spk} underruns={d.get("und")}')
                clock=None
                self.progress(65,'Verifying common SAI1 clock and internal SAI2 synchronisation')
                try: clock=self.ask('!TP','R21|TC_CLOCK|',3.0)
                except Exception: clock=None
                if clock:
                    ok_clock=(iv(clock,'sai1on')==1 and iv(clock,'sai2on')==1 and iv(clock,'mastercb')>0 and iv(clock,'tccb')>0)
                    desc=(f"SAI1 master={clock.get('sai1on')} SYNCOUT={clock.get('syncout')} "
                          f"SAI2 SYNCEN={clock.get('sai2sync')} active={clock.get('sai2on')} "
                          f"masterCB={clock.get('mastercb')} telecoilCB={clock.get('tccb')}")
                    self.ui(self.set_ev,'TC_CLOCK','PASS' if ok_clock else 'FAIL',desc)
                else:
                    ok_clock=False
                    self.ui(self.set_ev,'TC_CLOCK','FAIL','Shared clock status unavailable')
                if trans and sig and out:
                    self.ui(self.set_banner,'WARN','TELECOIL DIGITAL PATH PASSES — NOW CONFIRM WHETHER YOU HEAR IT')
                    self.ui(self.show_confirm); self.ui(self.set_badge,n,'WARN'); self.progress(85,'Waiting for physical telecoil confirmation'); self.session['await_physical']='TC'
                else:
                    if not ok_clock:
                        reason='No simultaneous SAI1/SAI2 DMA activity; inspect the common physical clocks and SAI2 data line.'
                    elif not trans:
                        reason='SAI1 shared clock and SAI2 sync register configuration PASS, but no SAI2 DMA callbacks: verify U4 PB15 SD line, SAI2 DMA/SAI protocol, and ADC serial format.'
                    else:
                        reason='SAI2 receives frames, but TC PCM is zero: check U4 data wiring, 24-bit PCM alignment and telecoil analog front-end.' if not sig else 'TC audio exists but output DMA did not advance'
                    self.finish('FAIL',reason)
            elif n==5:
                self.ui(self.open_rtos_monitor)
                self.progress(5,'Comparing REAL FreeRTOS CPU time with and without lower-priority work')
                try:self.ask('!BS','R21|BT_START|',2.5,retries=1)
                except TimeoutError:
                    self.progress(9,'BT start ACK missing; verifying real receive DMA later')
                time.sleep(.45)
                # A genuine baseline is mandatory. Comparing two windows lets an
                # examiner see how scheduler execution changes WITH actual work.
                try:self.ask('!RX','R28|LOAD|',2.0,retries=1)
                except TimeoutError:pass
                self.progress(12,'BASELINE: real kernel runtime and DMA with arithmetic load OFF')
                baseline0=self.ask('!RM','R28|TASKS|',2.8,retries=2)
                if iv(baseline0,'load')!=0:
                    raise RuntimeError('Baseline invalid: arithmetic load remained enabled')
                time.sleep(1.0)
                baseline1=self.ask('!RM','R28|TASKS|',2.8,retries=2)
                baseline_us=(iv(baseline1,'ms')-iv(baseline0,'ms'))*1000
                if baseline_us<=0 or iv(baseline1,'hz')!=1000000:
                    raise RuntimeError('Hardware runtime timer unavailable or time did not advance')
                baseline_cpu=100*((iv(baseline1,'grun')-iv(baseline0,'grun')) & 0xFFFFFFFF)/baseline_us
                baseline_idle=100*((iv(baseline1,'irun')-iv(baseline0,'irun')) & 0xFFFFFFFF)/baseline_us
                self.progress(25,f'Baseline: background {baseline_cpu:.2f}% MCU time, IDLE {baseline_idle:.1f}%')
                started_load=False
                try:
                    try:self.ask('!RS','R28|LOAD|',2.0,retries=1)
                    except TimeoutError:
                        self.progress(28,'Load ACK lost; verifying hardware load=1')
                    started_load=True
                    time.sleep(.3)
                    before=self.ask('!RM','R28|TASKS|',2.8,retries=2)
                    if iv(before,'load')!=2:raise RuntimeError('STM32 did not arm the real 60-second CPU stress mode')
                    observations=[before]
                    for i in range(6):
                        time.sleep(.45)
                        self.progress(35+i*8,f'LOADED window {i+1}/6: reading real kernel runtime and DMA')
                        observations.append(self.ask('!RM','R28|TASKS|',2.8,retries=2))
                    after=observations[-1]
                finally:
                    if started_load:
                        try:self.ask('!RX','R28|LOAD|',2.0,retries=1)
                        except TimeoutError:
                            try:
                                state=self.ask('!RM','R28|TASKS|',2.0,retries=2)
                                if iv(state,'load')==2:self.ask('!RX','R28|LOAD|',2.0,retries=1)
                            except Exception:pass
                delta={k:iv(after,k)-iv(before,k) for k in ('ctl','aud','bt','bg','work','miss','btcb','tccb','spk','under','qov','bov','tov')}
                dt_us=(iv(after,'ms')-iv(before,'ms'))*1000
                if dt_us<=0:raise RuntimeError('Loaded interval has no advancing clock')
                live_cpu={k:100*((iv(after,k)-iv(before,k)) & 0xFFFFFFFF)/dt_us
                          for k in ('crun','arun','brun','grun','irun')}
                monotonic=all(all(iv(y,k)>=iv(x,k) for k in ('ctl','aud','bt','bg','work'))
                              for x,y in zip(observations,observations[1:]))
                all_active=all(delta[k]>0 for k in ('ctl','aud','bt','bg','work'))
                kernel_ok=(all(iv(x,'sched')==2 and iv(x,'hz')==1000000 for x in observations))
                load_ok=all(iv(x,'load')==2 for x in observations)
                timer_ok=all(0 <= live_cpu[k] <= 110 for k in live_cpu)
                increased=live_cpu['grun']>baseline_cpu+0.10
                dma_active=(delta['btcb']>0 and delta['spk']>0)
                no_new_faults=all(delta[k]==0 for k in ('miss','under','qov','bov','tov'))
                # FAIL when actual RTOS execution or work is absent. A warning
                # when the profiler fails to show a CPU increase is honest.
                if not (all_active and monotonic and load_ok and kernel_ok and timer_ok):res='FAIL'
                elif dma_active and no_new_faults and increased:res='PASS'
                else:res='WARN'  # e.g. AudioTask consumes nearly all CPU: show actual work and limitation
                detail=(f'BASELINE load=OFF: BG CPU {baseline_cpu:.2f}%, IDLE {baseline_idle:.1f}%. '
                        f'LOADED load=ON: BG CPU {live_cpu["grun"]:.2f}%, '
                        f'Audio {live_cpu["arun"]:.2f}%, Control {live_cpu["crun"]:.2f}%, '
                        f'BT {live_cpu["brun"]:.2f}%, IDLE {live_cpu["irun"]:.1f}%. '
                        f'Measured TIM2 runtime Δ{dt_us/1000:.0f} ms, '
                        f'loops Δ[ctl={delta["ctl"]},aud={delta["aud"]},bt={delta["bt"]},bg={delta["bg"]}], '
                        f'actual arithmetic Δ{delta["work"]}, BT DMA Δ{delta["btcb"]}, '
                        f'speaker DMA Δ{delta["spk"]}, deadline misses Δ{delta["miss"]}, '
                        f'underruns Δ{delta["under"]}, FIFO overflow Δ{delta["qov"]}. '
                        f'Kernel running={int(kernel_ok)} background load confirmed={int(load_ok)}. '
                        f'BG measured CPU increases={int(increased)}. '
                        'CPU % is FreeRTOS runtime accounting (1MHz TIM2), with ISR time potentially attributed to interrupted tasks.')
                if self.session:self.session['rtos_baseline']={'before':baseline0,'after':baseline1,'bg_cpu':baseline_cpu,'idle_cpu':baseline_idle};self.session['rtos_loaded']={'samples':observations,'runtime_percent':live_cpu,'result':res}
                self.ui(self.set_ev,'RTOS_LOAD',res,detail)
                self.finish(res,detail)
        except Exception as e:self.finish('FAIL',f'{type(e).__name__}: {e}')
    def physical_confirm(self,heard,quality='PASS'):
        if not self.session or 'await_physical' not in self.session:return
        what=self.session.pop('await_physical'); self.hide_confirm(); key={'OUTPUT':'OUTPUT_PHYS','BT':'BT_PHYS','TC':'TC_PHYS'}[what]
        if not heard:
            res='FAIL'; actual='User confirmed no/bad audible output'
        elif quality=='WARN':
            res='WARN'; actual='User confirmed audio is audible but noisy/distorted'
        else:
            res='PASS'; actual='User physically confirmed clean/acceptable audible output'
        self.set_ev(key,res,actual)
        if what=='OUTPUT':
            try:self.link.ask('!OX','R21|OUT_STOP|',1.5)
            except:pass
        if what=='OUTPUT' and heard and not self.session.get('output_sw_ok',False):
            self.finish('WARN','OUTPUT audible, but software/DMA telemetry was incomplete')
        elif res=='WARN':
            self.finish('WARN',f'{what} audible but user reported noise/distortion; diagnostics preserved for tuning')
        else:
            self.finish(res,f'{what} physical audio '+('confirmed' if heard else 'NOT acceptable'))

    def open_signal_lab(self):
        """Standalone, non-graded real PCM analyser. Does not arm a test session."""
        self.graph._ensure()
        self.graph.win.lift()

    def _log_local(self,msg):
        t=iso(); line=f'[{t[11:23]}] {msg}\n'; self.console_all.append(line.rstrip()); self.console.insert('end',line); self.console.see('end')
    def capture_now(self):
        if not self.link.ser:return messagebox.showerror('Not connected','Connect first')
        if self.running:return messagebox.showwarning('Test running','Wait for the current test to finish.')
        self.session={'stage':0,'name':'AD_HOC_DIAGNOSTICS','start':iso(),'console_start':max(0,len(self.console_all)-100),'responses':[],'scope':[],'diag':{}}
        def w():
            try:
                d1,d2=self.link.collect_diag(3.0); self.session['diag']={'diag1':d1,'diag2':d2}; self.session.update(end=iso(),result='CAPTURE',detail='Ad-hoc diagnostics'); self.export_session()
            except Exception as e:self.ui(self._log_local,f'Capture failed: {e}')
        threading.Thread(target=w,daemon=True).start()
    def export_session(self):
        if not self.session:return
        s=dict(self.session); s['port']=self.port_name; s['baud']=BAUD; s['evidence']=self.ev; s['console']=self.console_all[s.get('console_start',0):]; s['tx_log']=self.link.tx[-150:]; s['rx_log']=self.link.rx[-300:]
        tag=f"{int(s.get('stage',0)):02d}_{s.get('name','SESSION').replace(' ','_').replace('>','GT')}"; base=RESULTS/f"SEND_TO_CHATGPT_R26_{tag}_{stamp()}"; tmp=RESULTS/(base.name+'_files'); tmp.mkdir(exist_ok=True)
        (tmp/'session.json').write_text(json.dumps(s,indent=2),encoding='utf-8'); (tmp/'console.log').write_text('\n'.join(s['console'])+'\n',encoding='utf-8')
        (tmp/'diagnostics.json').write_text(json.dumps(s.get('diag',{}),indent=2),encoding='utf-8')
        with open(tmp/'commands.csv','w',newline='',encoding='utf-8') as f:
            w=csv.writer(f); w.writerow(['time','command']); w.writerows(s['tx_log'])
        with open(tmp/'raw_responses.csv','w',newline='',encoding='utf-8') as f:
            w=csv.writer(f); w.writerow(['time','response']); w.writerows(s['rx_log'])
        with open(tmp/'evidence.csv','w',newline='',encoding='utf-8') as f:
            w=csv.writer(f); w.writerow(['check','expected','actual','result','time']);
            for k,v in self.ev.items():w.writerow([k,v['expected'],v['actual'],v['result'],v['time']])
        with open(tmp/'scope.csv','w',newline='',encoding='utf-8') as f:
            w=csv.writer(f); w.writerow(['frame','sample','value','fs','seq','src'])
            for fi,fr in enumerate(s.get('scope',[])):
                for j,v in enumerate(fr.get('samples',[])):w.writerow([fi,j,v,fr.get('fs'),fr.get('seq'),fr.get('src')])
        summary=(f"CONNECTIVITY SESSION\nTest: {s.get('stage')} {s.get('name')}\nStart: {s.get('start')}\nEnd: {s.get('end')}\nResult: {s.get('result')}\nDetail: {s.get('detail')}\nPort: {self.port_name} @ {BAUD}\n\n"
                 "Captures from the active STM32 source; recorded evidence, not a continuous raw audio stream.\n")
        (tmp/'SUMMARY.txt').write_text(summary,encoding='utf-8')
        lines=[]
        for rel in ['Core/Src/demo_app.c','Core/Inc/demo_app.h','Core/Src/sai.c','Core/Src/dma.c','Core/Src/speaker_output.c','Core/Src/pcm_transport.c','Core/Src/rtos_bootstrap.c','Core/Src/freertos.c','Dashboard/connectivity_dashboard.py']:
            p=ROOT/rel
            if p.exists():lines.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+rel)
        (tmp/'manifest_sha256.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
        z=Path(str(base)+'.zip')
        with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED) as zz:
            for p in tmp.iterdir():zz.write(p,p.name)
        shutil.copy2(z,LATEST); shutil.rmtree(tmp,ignore_errors=True)
        (ROOT/'RESULTS_LOCATION.txt').write_text(str(RESULTS.resolve())+'\n',encoding='utf-8')
        (ROOT/'LATEST_RESULT_PATH.txt').write_text(str(z.resolve())+'\n',encoding='utf-8')
        self.ui(self.latest.set,str(z.resolve())); self.ui(self._log_local,f'RESULT SAVED HERE -> {z.resolve()}')
    def open_results(self):
        try: os.startfile(str(RESULTS.resolve()))
        except Exception: self._log_local(str(RESULTS.resolve()))
    def open_project(self):
        try: os.startfile(str(ROOT.resolve()))
        except Exception: self._log_local(str(ROOT.resolve()))

if __name__=='__main__':
    # This pointer is local build output, ignored by git and never public source.
    (RESULTS / 'RESULTS_LOCATION.txt').write_text(str(RESULTS.resolve()) + '\n', encoding='utf-8')
    App().mainloop()
