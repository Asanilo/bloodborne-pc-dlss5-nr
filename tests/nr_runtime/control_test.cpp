#include "nr_runtime_control.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>
#include <unistd.h>
using namespace BbNR;
int main(){
 Config defaults;assert(defaults==Config{});assert(Validate(defaults));auto invalid=defaults;invalid.display_strength=std::numeric_limits<float>::quiet_NaN();auto before=ReadRequest();assert(!Submit(invalid));assert(ReadRequest().revision==before.revision);
 invalid=defaults;invalid.model_style=3;assert(!Submit(invalid));invalid=defaults;invalid.model_style=2;assert(Validate(invalid));invalid=defaults;invalid.ttl_frames=13;assert(!Submit(invalid));invalid=defaults;invalid.ttl_ms=201;assert(!Submit(invalid));invalid=defaults;invalid.min_confidence=-.1f;assert(!Submit(invalid));
 assert(Submit(defaults));auto a=ReadRequest();auto c=defaults;c.display_strength=.75f;assert(Submit(c));auto b=ReadRequest();assert(b.revision==a.revision+1&&b.reset_seq==a.reset_seq);
 c.ttl_frames=10;assert(Submit(c));auto r=ReadRequest();assert(r.reset_seq==b.reset_seq+1);auto seq=r.model_apply_seq;c.model_intensity=.6f;assert(Submit(c));assert(ReadRequest().model_apply_seq==seq);assert(Submit(c,{.apply_model=true}));r=ReadRequest();assert(r.model_apply_seq==seq+1&&r.model_config.model_intensity==.6f);c.model_intensity=.8f;assert(Submit(c));assert(ReadRequest().model_config.model_intensity==.6f);auto pending=ReadRequest();assert(Submit(pending.model_config,{.apply_model=true}));assert(ReadRequest().model_apply_seq==pending.model_apply_seq);Snapshot failed;failed.status=Status::Failed;assert(PublishSnapshot(failed));assert(Submit(pending.model_config,{.apply_model=true}));assert(ReadRequest().model_apply_seq==pending.model_apply_seq+1);
 auto cap=ReadRequest().capture_seq;constexpr int threads=8,each=500;std::vector<std::thread> pool;for(int t=0;t<threads;t++)pool.emplace_back([&]{for(int i=0;i<each;i++){auto local=ReadRequest();assert(Submit(local.config,{.capture=true}));auto s=ReadSnapshot();assert(s.version==ApiVersion);}});for(auto&t:pool)t.join();assert(ReadRequest().capture_seq==cap+threads*each);
 Snapshot s;s.applied_revision=ReadRequest().revision+1;assert(!PublishSnapshot(s));s.applied_revision=ReadRequest().revision;s.status=Status::Applied;assert(PublishSnapshot(s));
 for(uint64_t i=1;i<=300;i++)NotifyPresent(i,i,1'000'000'000+i*10'000'000);NotifyPresent(300,300,8'000'000'000);NotifyPresent(299,301,9'000'000'000);s=ReadSnapshot();assert(s.present_samples==256&&s.timing_available&&std::abs(s.fps-100)<.001&&s.p95_ms==10);
 Snapshot gpu;gpu.applied_revision=ReadRequest().revision;gpu.gpu_sample_available=true;gpu.gpu_sample_source=300;gpu.changed_fraction=.25f;gpu.history_valid_rate=.75f;assert(PublishSnapshot(gpu));auto sampled=ReadSnapshot();assert(sampled.timing_available&&!sampled.gpu_timing_available&&sampled.gpu_sample_available&&sampled.changed_fraction==.25f);
 gpu.gpu_timing_available=true;assert(!PublishSnapshot(gpu));gpu.gpu_timing_source=300;gpu.gpu_timing_monotonic_ns=4'000'000'000;gpu.history_pass_ms[2]=.4;gpu.model_refresh_available=true;gpu.model_refresh_source=300;gpu.model_refresh_ms=100;assert(PublishSnapshot(gpu));sampled=ReadSnapshot();assert(sampled.gpu_timing_available&&sampled.history_pass_ms[2]==.4&&sampled.model_refresh_ms==100);
 gpu.history_pass_ms[2]=std::numeric_limits<double>::infinity();assert(!PublishSnapshot(gpu));gpu.history_pass_ms[2]=.4;gpu.status=Status::Held;assert(PublishSnapshot(gpu));auto held=ReadRequest();assert(!Submit(held.config,{.apply_model=true}));assert(ReadRequest().model_apply_seq==held.model_apply_seq);gpu.status=Status::Failed;gpu.model_restart_required=true;assert(PublishSnapshot(gpu));assert(!Submit(held.config,{.apply_model=true}));gpu.model_restart_required=false;assert(PublishSnapshot(gpu));assert(Submit(held.config,{.apply_model=true}));
 auto dir=std::filesystem::temp_directory_path()/("nr-runtime-cpu-"+std::to_string(getpid()));std::filesystem::create_directory(dir);auto path=dir/"nrbridge.json";std::string error;assert(InitializeConfig(path.string(),&error));assert(ReadRequest().config==defaults);c=defaults;c.display_strength=.5f;assert(Submit(c));ShutdownConfig();assert(std::filesystem::exists(path));assert(InitializeConfig(path.string()));assert(ReadRequest().config==c);ShutdownConfig();
 {std::ofstream f(path);f<<"{\"version\":1}";}assert(!InitializeConfig(path.string(),&error));assert(ReadRequest().config==defaults);assert(!error.empty());ShutdownConfig();std::filesystem::remove_all(dir);
}
