"""Explicit daily online selection; account data stays in the private profile/child env."""
import json
import os
from pathlib import Path

SECRET_ENV = frozenset(('BB_THD_TOKEN', 'BB_SHADNET_PASSWORD', 'BB_SHADNET_TOKEN'))
FIELDS = ('thd_server', 'thd_online_id', 'thd_token', 'online_server', 'online_community',
          'online_webapi', 'online_npid', 'online_password', 'online_token', 'online_upnp')


def add_arguments(parser):
    parser.add_argument('--online-pool', choices=('offline', 'thd', 'shadnet'), default='offline')
    parser.add_argument('--online-account-config', help='explicit canonical launcher settings JSON; secrets stay private')
    parser.add_argument('--bbsc', action='store_true', help='explicit shadnet BBSC hooks; native mouse will fall back')


def validate_args(args):
    pool = getattr(args, 'online_pool', 'offline')
    account = getattr(args, 'online_account_config', None)
    bbsc = getattr(args, 'bbsc', False)
    if pool not in ('offline', 'thd', 'shadnet') or type(bbsc) is not bool:
        raise ValueError('online pool/BBSC selection invalid')
    if (pool != 'offline') != bool(account):
        raise ValueError('online pool and explicit account configuration must be selected together')
    if pool != 'offline' and getattr(args, 'daily', False) is not True:
        raise ValueError('online pool selection requires daily mode')
    if bbsc and pool != 'shadnet':
        raise ValueError('BBSC requires the explicit shadnet pool')


def read_account(value):
    if not isinstance(value, str) or not value or any(c in value for c in ('\x00', '\n', ':')):
        raise ValueError('canonical absolute online account configuration required')
    path = Path(value)
    if not path.is_absolute() or path.resolve() != path or path.is_symlink() or not path.is_file() or path.stat().st_size > 65536:
        raise ValueError('bounded canonical regular online account configuration required')
    try:
        record = json.loads(path.read_text())
    except (ValueError, OSError):
        raise ValueError('cannot read online account configuration') from None
    if not isinstance(record, dict):
        raise ValueError('online account configuration must be an object')
    chosen = {k: record[k] for k in FIELDS if k in record}
    if any((type(v) is not bool if k == 'online_upnp' else
            not isinstance(v, str) or len(v) > 8192 or any(c in v for c in ('\x00','\n','\r')))
           for k, v in chosen.items()):
        raise ValueError('invalid online account field types')
    if any('@' in chosen.get(k, '') for k in ('thd_server','online_server','online_community','online_webapi')):
        raise ValueError('server addresses must not embed account credentials')
    return chosen


def prepare(args, build, profile):
    validate_args(args)
    pool = getattr(args, 'online_pool', 'offline')
    if pool == 'offline':
        return {}, {'pool': 'offline'}, None
    settings = read_account(args.online_account_config)
    module = build / 'gpu/libbbnet20.so'
    if module.resolve() != module or module.is_symlink() or not module.is_file():
        raise ValueError('selected Native build has no canonical online module')
    env = {'BB_ONLINE': '1', 'BB_NET_GEN': '20', 'BB_NET_MODULE': str(module),
           'BB_SKIP_NETWORK_CHOICE': '0'}
    if pool == 'thd':
        if not settings.get('thd_online_id') or not settings.get('thd_token'):
            raise ValueError('THD account missing; sign in through the launcher first')
        env.update(BB_NET_BACKEND='thd', BB_COMMUNITY_SERVER=settings.get('thd_server') or 'https://thehuntersdream.com',
                   BB_THD_ONLINE_ID=settings['thd_online_id'], BB_THD_TOKEN=settings['thd_token'])
        selected = {k: settings[k] for k in ('thd_server', 'thd_online_id', 'thd_token') if k in settings}
    else:
        if not settings.get('online_npid') or not (settings.get('online_password') or settings.get('online_token')):
            raise ValueError('shadnet account missing; configure it through the launcher first')
        env.update(BB_SHADNET_SERVER=settings.get('online_server') or 'play.seamlesshunt.com:31313',
                   BB_SHADNET_WEBAPI=settings.get('online_webapi') or '',
                   BB_COMMUNITY_SERVER=settings.get('online_community') or 'http://play.seamlesshunt.com',
                   BB_SHADNET_NPID=settings['online_npid'],
                   BB_UPNP='1' if settings.get('online_upnp', False) else '0')
        if settings.get('online_password'):
            env['BB_SHADNET_PASSWORD'] = settings['online_password']
        if settings.get('online_token'):
            env['BB_SHADNET_TOKEN'] = settings['online_token']
        if args.bbsc:
            env['BB_BBSC'] = '1'
        selected = {k: v for k, v in settings.items() if k.startswith('online_')}
    account = profile / 'online-account.json'
    descriptor = os.open(account, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, 'w') as stream:
        json.dump(selected, stream)
        stream.write('\n')
    return env, {'pool': pool, 'bbsc': args.bbsc, 'account': str(account),
                 'account_secrets': 'private profile only; manifest environment redacted'}, module


def public_environment(environment):
    return {key: value for key, value in environment.items() if key not in SECRET_ENV}


def validate_manifest(run, manifest):
    record = manifest.get('online', {'pool': 'offline'})
    pool = record.get('pool')
    environment = manifest.get('environment', {})
    if pool == 'offline':
        if any(key in environment for key in ('BB_ONLINE','BB_NET_MODULE','BB_BBSC','BB_NET_BACKEND',*SECRET_ENV)):
            raise ValueError('offline daily environment drift')
        return
    if pool not in ('thd','shadnet') or type(record.get('bbsc')) is not bool or (pool == 'thd' and record['bbsc']):
        raise ValueError('daily online selection drift')
    account = run / 'profile/online-account.json'
    if (record.get('account') != str(account) or account.resolve() != account or account.is_symlink()
            or not account.is_file() or account.stat().st_mode & 0o777 != 0o600 or account.stat().st_nlink != 1):
        raise ValueError('private online account configuration drift')
    expected = {'BB_ONLINE':'1','BB_NET_GEN':'20','BB_NET_MODULE':str(Path(manifest['native_build'])/'gpu/libbbnet20.so')}
    if (any(environment.get(k) != v for k,v in expected.items())
            or any(key in environment for key in SECRET_ENV)
            or environment.get('BB_BBSC') != ('1' if record['bbsc'] else None)
            or environment.get('BB_NET_BACKEND') != ('thd' if pool == 'thd' else None)):
        raise ValueError('daily online environment drift')
