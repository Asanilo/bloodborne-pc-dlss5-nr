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
 def test_8gb_limit_and_bad_input(self):
  for opts in [('--nr-output','2560x1440'),('--nr-sr-preset','dlaa','--nr-output','3840x2160'),('--nr-sr-preset','quality','--nr-output','1921x1080'),('--nr-sr-preset','quality','--nr-output','0x0')]:
   with self.assertRaises(ValueError):dims.select(self.args(*opts))
