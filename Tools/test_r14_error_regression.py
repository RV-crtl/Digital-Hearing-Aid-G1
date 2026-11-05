from pathlib import Path
import subprocess,sys,shutil,tempfile
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'Tools/r14_build_guard.py')],check=True)
it=(root/'Core/Src/stm32l4xx_it.c').read_text()
assert 'hdma_dfsdm1_flt0' not in it, 'BUG: generated DMA handle from user R13 log survived'
assert 'void DMA1_Channel4_IRQHandler' not in it
assert 'void DMA1_Channel4_IRQHandler' in (root/'Core/Src/g1_product_board.c').read_text()
# A simulated CubeMX generation creates an invalid IRQ and a standalone i2c.c.
# An actual fresh CubeIDE build must be run on the user's installed 1.19.0.
print('R14 previously failing DFSDM DMA symbol regression PASS')
