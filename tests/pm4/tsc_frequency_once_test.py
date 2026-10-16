"""Exercise actual runtime calibration under overlapping first callers; no GPU."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / 'src/runtime_kernel.c').read_text()
start = source.index('static uint64_t tsc_hz;')
end = source.index('/* Shared with the GPU library', start)
actual = source[start:end]
legacy = actual.replace('static pthread_once_t tsc_once = PTHREAD_ONCE_INIT;', '')
legacy = legacy.replace('pthread_once(&tsc_once,calibrate_tsc_frequency);',
                        'if (!tsc_hz) calibrate_tsc_frequency();')
assert legacy != actual
prefix = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <time.h>
#define ABI
static sem_t first_calibrating, release_first;
static _Atomic unsigned calibrations;
static _Thread_local unsigned worker, clock_reads, tsc_reads;
static int fake_clock(clockid_t id, struct timespec* t) {
    (void)id; t->tv_sec=0; t->tv_nsec=clock_reads++ ? 1000000 : 0; return 0;
}
static uint64_t fake_tsc(void) {
    return 100+(tsc_reads++ ? (worker==1 ? 2000000 : 3000000) : 0);
}
static int fake_sleep(const struct timespec* request, struct timespec* remain) {
    (void)request;(void)remain;atomic_fetch_add(&calibrations,1);
    if(worker==1){sem_post(&first_calibrating);sem_wait(&release_first);}
    return 0;
}
#define clock_gettime fake_clock
#define __rdtsc fake_tsc
#define nanosleep fake_sleep
'''
suffix = r'''
static uint64_t result[2];
static void* read_frequency(void* arg) {
    worker=(unsigned)(uintptr_t)arg;
    result[worker-1]=tsc_frequency();return 0;
}
int main(void) {
    assert(!sem_init(&first_calibrating,0,0));assert(!sem_init(&release_first,0,0));
    pthread_t first,second;
    assert(!pthread_create(&first,0,read_frequency,(void*)(uintptr_t)1));
    sem_wait(&first_calibrating);
    assert(!pthread_create(&second,0,read_frequency,(void*)(uintptr_t)2));
    // Legacy's second caller publishes 3GHz before the first overwrites it with
    // 2GHz. Joining before release makes this negative control deterministic
    // without unsynchronized concurrent writes in the test itself.
    if(LEGACY) assert(!pthread_join(second,0));
    sem_post(&release_first);assert(!pthread_join(first,0));
    if(!LEGACY) assert(!pthread_join(second,0));
    assert(result[0]==2000000000);
    assert(result[1]==(LEGACY ? 3000000000 : 2000000000));
    assert(calibrations==(LEGACY ? 2u : 1u));
    assert(tsc_frequency()==2000000000);
    assert(calibrations==(LEGACY ? 2u : 1u));
    sem_destroy(&first_calibrating);sem_destroy(&release_first);
}
'''
with tempfile.TemporaryDirectory(prefix='nr-tsc-once-') as directory:
    root = Path(directory)
    for name, body, old in [('legacy', legacy, 1), ('actual', actual, 0)]:
        cpp = root / (name + '.c')
        exe = root / name
        cpp.write_text(prefix + body + suffix.replace('LEGACY', str(old)))
        subprocess.run(['cc', '-std=c11', '-O2', '-pthread', '-Wall', '-Wextra',
                        '-Werror', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
print('Actual TSC initialization PASS; legacy publishes conflicting calibrations; once shares one')
