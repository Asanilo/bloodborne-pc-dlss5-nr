import argparse,importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('dimensions',Path(__file__).resolve().parents[1]/'dimension_profile.py');dims=importlib.util.module_from_spec(spec);spec.loader.exec_module(dims)
class Dimensions(unittest.TestCase):
 def args(self,*argv):
  p=argparse.ArgumentParser();dims.add_arguments(p);return p.parse_args(argv)
 def test_legacy_is_unchanged(self):
  self.assertIsNone(dims.select(self.args()));self.assertEqual(dims.environment(None),{})
 def test_render_scale_even_and_extent_identity(self):
  p=dims.select(self.args('--nr-sr-preset','balanced'));self.assertEqual((p['render_width'],p['render_height']),(1130,636));self.assertEqual(p['render_width'],p['model_width'])
  p=dims.select(self.args('--nr-sr-preset','quality','--nr-output','2560x1440'));self.assertEqual((p['model_width'],p['model_height']),(1706,960))
 def test_dlaa_and_aspect(self):
  p=dims.select(self.args('--nr-sr-preset','dlaa'));self.assertEqual(p['model_width'],p['output_width'])
  p=dims.select(self.args('--nr-sr-preset','performance','--nr-output','2560x1080'));self.assertEqual((p['model_width'],p['model_height']),(1280,540))
 def test_bounded_execution_is_independent_of_sr(self):
  p=dims.select(self.args('--nr-sr-preset','dlaa','--nr-output','2560x1440'));self.assertEqual((p['render_width'],p['render_height']),(2560,1440));self.assertEqual((p['model_width'],p['model_height']),(1920,1080))
  p=dims.select(self.args('--nr-sr-preset','performance','--nr-execution','1280x720'));self.assertEqual(p['render_width'],960);self.assertEqual(p['model_width'],1280)
 def test_8gb_limit_and_bad_input(self):
  for opts in [('--nr-output','2560x1440'),('--nr-sr-preset','dlaa','--nr-output','7680x4320'),('--nr-sr-preset','quality','--nr-output','1921x1080'),('--nr-sr-preset','quality','--nr-output','0x0')]:
   with self.assertRaises(ValueError):dims.select(self.args(*opts))

 def test_private_ini_keeps_controls_and_selects_real_aspect(self):
  p=dims.select(self.args('--nr-sr-preset','quality','--nr-output','1920x1200'))
  ini=dims.ini(p,'key.cross=Space\nsharpen=0.3\naspect_ratio=auto\npreset=3\n')
  self.assertIn('key.cross=Space',ini);self.assertIn('sharpen=0.3',ini)
  self.assertIn('aspect_ratio=16:10',ini);self.assertIn('live_resolution=1',ini);self.assertEqual(dims.environment(p)['BB_LIVE_RES'],'1');self.assertEqual(ini.count('preset='),1)
  with self.assertRaisesRegex(ValueError,'output menu'):dims.select(self.args('--nr-sr-preset','quality','--nr-output','1900x1000'))

 def test_native_only_has_no_sr_and_preserves_unused_player_preset(self):
  profile=dims.select(self.args('--nr-sr-preset','native','--nr-output','2560x1440','--nr-execution','1280x720'))
  self.assertFalse(profile['sr_enabled']);self.assertEqual((profile['render_width'],profile['render_height']),(2560,1440))
  env=dims.environment(profile);self.assertEqual(env['BB_UPSCALER'],'off');self.assertNotIn('BB_UPSCALE_PRESET',env)
  ini=dims.ini(profile,'preset=3\nkey.cross=Space\n')
  self.assertIn('preset=3',ini);self.assertIn('upscaler=off',ini);self.assertIn('key.cross=Space',ini)
