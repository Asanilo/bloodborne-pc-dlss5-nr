"""Explicit M5 extent contract. No GPU access, runtime copying or legacy-default changes."""
import math,re
PRESETS={'dlaa':0,'quality':1,'balanced':2,'performance':3,'ultra-performance':4}
SCALES=(1.,1.5,1.7,2.,3.)
def add_arguments(p):
 p.add_argument('--nr-sr-preset',choices=tuple(PRESETS),help='opt-in M5 NR execution/SR preset; requires dimensions-v3 Native and Helper')
 p.add_argument('--nr-output',help='M5 SR output WIDTHxHEIGHT; defaults to 1920x1080')
def select(args):
 name=getattr(args,'nr_sr_preset',None);output=getattr(args,'nr_output',None)
 if name is None and output is None:return None
 if name not in PRESETS:raise ValueError('--nr-output requires an explicit --nr-sr-preset')
 match=re.fullmatch(r'([0-9]{3,4})x([0-9]{3,4})',output or '1920x1080')
 if not match:raise ValueError('M5 output must be WIDTHxHEIGHT')
 ow,oh=map(int,match.groups());preset=PRESETS[name]
 if not(640<=ow<=7680 and 360<=oh<=4320 and ow%2==oh%2==0 and .8<=ow/oh<=3.6):raise ValueError('M5 output dimensions/aspect out of bounds')
 w,h=(max(1,math.floor(x/SCALES[preset]/2+.5))*2 for x in (ow,oh))
 if w<320 or h<180 or w*h>1920*1080:raise ValueError('NR execution exceeds 8GiB working-set policy; select a lower SR render preset')
 return {'schema':'nr-dimensions-v3','preset':preset,'preset_name':name,'render_width':w,'render_height':h,'model_width':w,'model_height':h,'output_width':ow,'output_height':oh,'estimated_nr_image_bytes':w*h*768,'color_domain':'scene_hdr'}
def environment(profile):
 if profile is None:return {}
 return {'BB_NR_DIMENSIONS_V3':'1','BB_NR_MODEL_WIDTH':str(profile['model_width']),'BB_NR_MODEL_HEIGHT':str(profile['model_height']),'BB_NR_SR_OUTPUT_WIDTH':str(profile['output_width']),'BB_NR_SR_OUTPUT_HEIGHT':str(profile['output_height']),'BB_UPSCALE_PRESET':str(profile['preset'])}
def validate_capabilities(profile,build,binding):
 if profile is None:return
 if b'NRBRIDGE_NATIVE_DIMENSIONS_V3' not in (build/'gpu/libbbgpu.so').read_bytes():raise ValueError('M5 dimensions require a rebuilt dimensions-v3 Native')
 if binding is None or 'nr_dimensions.h' not in binding['helper_build'].get('protocol',{}).get('exports',{}):
  # Binding manifests retain their path+SHA; verify the actual build record.
  import json
  protocol=json.loads(__import__('pathlib').Path(binding['helper_build']['path']).read_text()).get('protocol',{}) if binding else {}
  if 'nr_dimensions.h' not in protocol.get('exports',{}):raise ValueError('M5 dimensions require a rebuilt dimensions-v3 Helper')
def ini(profile):
 if profile is None:return None
 return f'upscaler=dlss\npreset={profile["preset"]}\naspect_ratio=auto\noutput_res={profile["output_width"]}x{profile["output_height"]}\njitter=1\nobject_motion=1\nsharpen=0\nshow_fps=1\n'
