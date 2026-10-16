function metrics = verify_audio_filters(csvPath, outputDir)
% VERIFY_AUDIO_FILTERS: compare real production-C audio_dynamics.c output
% against independent double-precision MATLAB biquad calculations.
% Prerequisite: compile/run Validation/c/audio_vectors.c; see ../README.md.
% No Signal Processing Toolbox is required. No hardware measurement claimed.
if nargin < 1 || isempty(csvPath), csvPath = fullfile('Validation','results','audio_vectors.csv'); end
if nargin < 2 || isempty(outputDir), outputDir = fullfile('Validation','results'); end
if ~exist(outputDir,'dir'), mkdir(outputDir); end
T = readtable(csvPath, 'TextType','string');
fs = 48000;
hp = [0.9862119246 -1.9724238493 0.9862119246];
hpa = [1 -1.9722337292 0.9726139693];
eq = [0.9727371367 -1.7736777085 0.8405655869];
eqa = [1 -1.7736777085 0.8133027236];
configs = {'hpf','hpf_eq'};
errors = zeros(1,2);
for k=1:2
    selector = strcmp(string(T.configuration), configs{k});
    x = double(T.input(selector));
    c = double(T.output(selector));
    assert(numel(x)==4096, 'Missing or duplicated production-C samples');
    reference = filter(hp,hpa,x);
    if k==2, reference=filter(eq,eqa,reference); end
    errors(k) = max(abs(c-reference));
    assert(errors(k)<3.0,'Filter mismatch exceeds 3 PCM LSB');
    fig=figure('Visible','off','Color','w');
    plot((0:255)'/fs*1e3,c(1:256),'LineWidth',1.1);hold on;
    plot((0:255)'/fs*1e3,reference(1:256),'--','LineWidth',1.1);
    xlabel('Time (ms)');ylabel('PCM counts');legend('Production C','MATLAB reference');
    title(['Impulse: ' configs{k}]);grid on;
    saveas(fig,fullfile(outputDir,['impulse_' configs{k} '.png']));close(fig);
end
f=linspace(1,10000,1500);z=exp(-1j*2*pi*f/fs);
H=(hp(1)+hp(2)*z+hp(3)*z.^2)./(hpa(1)+hpa(2)*z+hpa(3)*z.^2);
E=(eq(1)+eq(2)*z+eq(3)*z.^2)./(eqa(1)+eqa(2)*z+eqa(3)*z.^2);
fig=figure('Visible','off','Color','w');
semilogx(f,20*log10(max(abs(H),1e-9)),'LineWidth',1.5);hold on;
semilogx(f,20*log10(max(abs(H.*E),1e-9)),'LineWidth',1.5);
grid on;xlabel('Frequency (Hz)');ylabel('Magnitude (dB)');
legend('150 Hz high pass','High pass + 1.6 kHz EQ','Location','best');
title('Analytical response from production coefficients');
saveas(fig,fullfile(outputDir,'filter_frequency_response.png'));close(fig);
metrics = struct('hpf_max_lsb_error', errors(1), ...
 'hpf_eq_max_lsb_error', errors(2), ...
 'hpf_20hz_db', 20*log10(abs(interp1(f,H,20))), ...
 'hpf_1khz_db', 20*log10(abs(interp1(f,H,1000))), ...
 'eq_1p6khz_db', 20*log10(abs(interp1(f,E,1600))));
assert(metrics.hpf_20hz_db < -25);
assert(metrics.hpf_1khz_db > -0.5);
assert(metrics.eq_1p6khz_db > -4 && metrics.eq_1p6khz_db < -2);
disp(metrics);
end
