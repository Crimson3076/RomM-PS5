from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
MINIZ = ROOT / 'app/romm_ui/vendor/miniz'
MINIZ_ARGS = ['-DMINIZ_NO_DEFLATE_APIS', '-DMINIZ_NO_ZLIB_APIS', '-DMINIZ_NO_TIME'] + [str(MINIZ / f) for f in ('miniz.c', 'miniz_tinfl.c')] + [str(ROOT / 'app/romm_ui/miniz_reader.c')]
