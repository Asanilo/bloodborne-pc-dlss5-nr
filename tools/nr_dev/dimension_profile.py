"""Explicit M5 extent contract. No GPU access, runtime copying or legacy-default changes."""
import math,re
PRESETS={'dlaa':0,'quality':1,'balanced':2,'performance':3,'ultra-performance':4}
SCALES=(1.,1.5,1.7,2.,3.)
# Match the game's supported output groups. Reject unknown modes rather than let
# CurrentOutput silently choose a different render/output contract.
OUTPUTS={
 '4:3':('640x480','800x600','1024x768','1280x960','1440x1080','1600x1200','2048x1536','2880x2160'),
 '16:10':('640x400','960x600','1280x800','1440x900','1680x1050','1920x1200','2560x1600','3840x2400'),
 '16:9':('640x360','854x480','960x540','1280x720','1600x900','1920x1080','2560x1440','3200x1800','3840x2160'),
 '21:9':('860x360','1280x540','1720x720','2560x1080','3440x1440','3840x1600','5120x2160'),
 '32:9':('1280x360','1920x540','2560x720','3840x1080','5120x1440','7680x2160')}
def aspect(output):
 for name,modes in OUTPUTS.items():
  if output in modes:return name
 raise ValueError('M5 output is not in the game output menu: '+output)
def add_arguments(p):
 p.add_argument('--nr-sr-preset',choices=tuple(PRESETS),help='opt-in M5 NR execution/SR preset; requires dimensions-v3 Native and Helper')
 p.add_argument('--nr-execution',help='optional bounded NR execution WIDTHxHEIGHT, independently of SR render/output')
 p.add_argument('--nr-output',help='M5 SR output WIDTHxHEIGHT; defaults to 1920x1080')
def select(args):
 name=getattr(args,'nr_sr_preset',None);output=getattr(args,'nr_output',None)
 if name is None and output is None and getattr(args,'nr_execution',None) is None:return None
 if name not in PRESETS:raise ValueError('--nr-output requires an explicit --nr-sr-preset')
 match=re.fullmatch(r'([0-9]{3,4})x([0-9]{3,4})',output or '1920x1080')
 if not match:raise ValueError('M5 output must be WIDTHxHEIGHT')
 ow,oh=map(int,match.groups());preset=PRESETS[name];ratio=aspect(f'{ow}x{oh}')
 if not(640<=ow<=7680 and 360<=oh<=4320 and ow%2==oh%2==0 and .8<=ow/oh<=3.6):raise ValueError('M5 output dimensions/aspect out of bounds')
 w,h=(max(1,math.floor(x/SCALES[preset]/2+.5))*2 for x in (ow,oh))
 if ow*oh>3840*2160:raise ValueError('M5 8GiB output policy is bounded to 4K pixels')
 mw,mh=w,h
 if getattr(args,'nr_execution',None):
  m=re.fullmatch(r'([0-9]{3,4})x([0-9]{3,4})',args.nr_execution)
  if not m:raise ValueError('NR execution must be WIDTHxHEIGHT')
  mw,mh=map(int,m.groups())
 elif mw*mh>1920*1080:
  scale=math.sqrt((1920*1080)/(mw*mh));mw,mh=math.floor(mw*scale/2)*2,math.floor(mh*scale/2)*2
 if mw<320 or mh<180 or mw>4096 or mh>4096 or mw%2 or mh%2 or mw*mh>1920*1080:raise ValueError('NR execution exceeds 8GiB working-set policy')
 return {'schema':'nr-dimensions-v3','aspect_ratio':ratio,'preset':preset,'preset_name':name,'render_width':w,'render_height':h,'model_width':mw,'model_height':mh,'output_width':ow,'output_height':oh,'estimated_nr_image_bytes':mw*mh*768,'color_domain':'scene_hdr'}
def environment(profile):
 if profile is None:return {}
 return {'BB_NR_DIMENSIONS_V3':'1','BB_NR_MODEL_WIDTH':str(profile['model_width']),'BB_NR_MODEL_HEIGHT':str(profile['model_height']),'BB_NR_SR_OUTPUT_WIDTH':str(profile['output_width']),'BB_NR_SR_OUTPUT_HEIGHT':str(profile['output_height']),'BB_UPSCALE_PRESET':str(profile['preset'])}
def validate_capabilities(profile,build,binding):
 if profile is None:return
 if binding and b'NRBRIDGE_DIMENSIONS_V3R2' not in __import__('pathlib').Path(binding['config']['helper']['path']).read_bytes():raise ValueError('M5 variable dimensions require the matching v3 revision-2 Helper')
 if b'NRBRIDGE_DIMENSIONS_V3R2' not in (build/'gpu/libbbgpu.so').read_bytes():raise ValueError('M5 dimensions require a rebuilt dimensions-v3 Native')
 if binding is None or 'nr_dimensions.h' not in binding['helper_build'].get('protocol',{}).get('exports',{}):
  # Binding manifests retain their path+SHA; verify the actual build record.
  import json
  protocol=json.loads(__import__('pathlib').Path(binding['helper_build']['path']).read_text()).get('protocol',{}) if binding else {}
  if 'nr_dimensions.h' not in protocol.get('exports',{}):raise ValueError('M5 dimensions require a rebuilt dimensions-v3 Helper')
def ini(profile,original=''):
 if profile is None:return None
 updates={'upscaler':'dlss','preset':str(profile['preset']),'aspect_ratio':profile['aspect_ratio'],
          'output_res':f"{profile['output_width']}x{profile['output_height']}", 'jitter':'1','object_motion':'1'}
 # Keep controls, language and other saved choices in the isolated run profile.
 lines=[]
 for line in original.splitlines():
  key=line.split('=',1)[0].strip() if '=' in line else ''
  if key in updates:continue
  lines.append(line)
 lines += [f'{key}={value}' for key,value in updates.items()]
 return '\n'.join(lines)+'\n'
