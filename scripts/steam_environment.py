"""Keep Steam's mixed-architecture overlay out of the managed launcher chain."""

MARKER = '# BBPORT_STEAM_OVERLAY_GUARD_V1'
GUARD = r'''# BBPORT_STEAM_OVERLAY_GUARD_V1
# Native play does not inherit Steam's overlay. Filter it before spawning any
# launcher tools, keeping other explicit preloads and all game arguments.
if [ -n "${LD_PRELOAD-}" ]; then
    _bb_saved_ifs=$IFS
    _bb_saved_flags=$-
    IFS=' :	
'
    set -f
    _bb_preload=
    for _bb_library in $LD_PRELOAD; do
        case ${_bb_library##*/} in
            gameoverlayrenderer.so) ;;
            *) _bb_preload="${_bb_preload:+$_bb_preload:}$_bb_library" ;;
        esac
    done
    IFS=$_bb_saved_ifs
    case $_bb_saved_flags in *f*) ;; *) set +f ;; esac
    if [ -n "$_bb_preload" ]; then
        LD_PRELOAD=$_bb_preload
        export LD_PRELOAD
    else
        unset LD_PRELOAD
    fi
    unset _bb_saved_ifs _bb_saved_flags _bb_preload _bb_library
fi
'''


def guard_shell(text):
    """Insert the builtin-only guard before the first external command."""
    if MARKER in text:
        return text
    first, separator, rest = text.partition('\n')
    if not first.startswith('#!') or not separator:
        raise ValueError('Expected a shell script with a shebang')
    return first + '\n' + GUARD + rest
