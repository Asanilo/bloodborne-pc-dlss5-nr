// SPDX-License-Identifier: GPL-3.0-or-later
// CPU regression of the metadata gate used by RecordCandidate; no Vulkan device.
#include "native_persistent_history_admission.h"
#include "native_persistent_history_shader.h"
#include "native_visual_timeline.h"
#include "nr_runtime_control.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <limits>
#include <vector>

namespace {
using BbNativeAsync::Frame;
using Vulkan::AdmitPersistentHistoryCandidate;
using Vulkan::PersistentHistoryAdmissionClock;
struct Link {Frame frame;};
struct Fixture {
 std::vector<Link> links;
 std::deque<PersistentHistoryAdmissionClock> clocks;
 Frame current;
 uint64_t minimum_origin=0;
 explicit Fixture(unsigned count=5){
  for(unsigned k=0;k<count;++k){
   Frame frame{.source=100+k,.previous=99+k,.epoch=7,.history=19,.params=3};
   links.push_back({frame});clocks.push_back({frame.source,1000.+k*16.,2,3});
  }
  if(count){current=links.back().frame;current.epoch=0;current.history=2;}
 }
 Vulkan::PersistentHistoryAdmission Admit()const {
  return AdmitPersistentHistoryCandidate(std::span<const Link>(links),clocks,current,minimum_origin);
 }
};

void RequestRolloverKeepsVisualHistory(){
 BbNativeAsync::VisualTimeline visual;
 BbNativeAsync::Chain request;
 std::deque<PersistentHistoryAdmissionClock> clocks;
 std::vector<Link> links;
 unsigned rollovers=0;
 for(uint64_t source=1;source<=18;++source){
  Frame raw{.source=source};const auto current=visual.Observe(raw);
  clocks.push_back({source,double(source)*16.,current.history,current.params});
  if(clocks.size()>16)clocks.pop_front();
  if(request.Classify(raw)==BbNativeAsync::Chain::Decision::Full){
   request.Restart(false);links.clear();++rollovers;
  }
  assert(request.Append(raw));links.push_back({raw});
  const auto result=AdmitPersistentHistoryCandidate(std::span<const Link>(links),clocks,current,0);
  assert(result.admitted&&current.history==1);
  assert(result.times[links.size()-1]-result.times[0]==double(links.size()-1)*16.);
 }
 assert(rollovers==3);
 // The request history counter may also differ after independent job resets.
 Fixture distinct;assert(distinct.links[0].frame.history!=distinct.current.history);
 assert(distinct.Admit().admitted);
}

void RequestBoundariesAndLineage(){
 for(unsigned count=1;count<=BbNativeAsync::NodeCount;++count){
  Fixture good(count);assert(good.Admit().admitted);
 }
 assert(!Fixture(0).Admit().admitted);
 assert(!Fixture(BbNativeAsync::NodeCount+1).Admit().admitted);
 Fixture missing;missing.links.erase(missing.links.begin()+2);assert(!missing.Admit().admitted);
 Fixture previous;previous.links[2].frame.previous=99;assert(!previous.Admit().admitted);
 Fixture final;final.current.source++;assert(!final.Admit().admitted);
 Fixture future;future.current.source=99;assert(!future.Admit().admitted);
 Fixture epoch;epoch.links[2].frame.epoch++;assert(!epoch.Admit().admitted);
 Fixture history;history.links[2].frame.history++;assert(!history.Admit().admitted);
 Fixture reset;reset.links[0].frame.sr_reset=true;assert(reset.Admit().admitted);
 reset.links[2].frame.sr_reset=true;assert(!reset.Admit().admitted);
 Fixture edited;edited.links[2].frame.source_edited=true;assert(!edited.Admit().admitted);
 Fixture invalid;invalid.links[2].frame.viewport[0]=std::numeric_limits<float>::infinity();assert(!invalid.Admit().admitted);
 Fixture zero;zero.links[0].frame.source=0;assert(!zero.Admit().admitted);
 Fixture zeroParams;zeroParams.current.params=0;assert(!zeroParams.Admit().admitted);
}

void ObservedIdentityAndResetFloor(){
 // A self-consistent request with the wrong model params used to pass: the
 // clocks were checked against current, but not against the retained Frames.
 Fixture wrongRequest;for(auto& link:wrongRequest.links)link.frame.params=8;
 assert(!wrongRequest.Admit().admitted);
 Fixture oneWrong;oneWrong.links[2].frame.params=8;assert(!oneWrong.Admit().admitted);
 Fixture wrongCurrent;wrongCurrent.current.params=8;assert(!wrongCurrent.Admit().admitted);
 for(unsigned k=0;k<5;++k){
  Fixture wrongClock;wrongClock.clocks[k].params=8;assert(!wrongClock.Admit().admitted);
  Fixture oldVisual;oldVisual.clocks[k].history=1;assert(!oldVisual.Admit().admitted);
  Fixture absent;absent.clocks.erase(absent.clocks.begin()+k);assert(!absent.Admit().admitted);
 }
 Fixture reset;Vulkan::PersistentHistoryResetWindow window;
 window.ResetAt(100);reset.minimum_origin=window.minimum_origin;assert(reset.Admit().admitted);
 window.ResetAt(101);reset.minimum_origin=window.minimum_origin;assert(!reset.Admit().admitted);
 window.ResetAt(90);reset.minimum_origin=window.minimum_origin;assert(!reset.Admit().admitted);
 // A fresh immutable origin at the floor can enter without rewriting its ID.
 reset.links.erase(reset.links.begin());assert(reset.Admit().admitted);
}

void ClockFailuresAndClosedTimeBoundary(){
 const double nan=std::numeric_limits<double>::quiet_NaN();
 const double inf=std::numeric_limits<double>::infinity();
 for(unsigned k=0;k<5;++k)for(double invalid:{nan,inf,-inf,-1.}){
  Fixture bad;bad.clocks[k].ms=invalid;assert(!bad.Admit().admitted);
 }
 // Positive total elapsed alone does not prove monotonic catch-up steps.
 Fixture reversedStep;reversedStep.clocks[2].ms=1008.;assert(!reversedStep.Admit().admitted);
 Fixture reversedTotal;for(unsigned k=0;k<5;++k)reversedTotal.clocks[k].ms=1000.-k;
 assert(!reversedTotal.Admit().admitted);
 Fixture boundary;for(unsigned k=0;k<5;++k)boundary.clocks[k].ms=k*50.;
 const auto admitted=boundary.Admit();assert(admitted.admitted);
 assert(admitted.times[4]-admitted.times[0]==200.);
 boundary.clocks[4].ms=std::nextafter(200.,inf);assert(!boundary.Admit().admitted);
 Fixture single(1);single.clocks[0].ms=0.;assert(single.Admit().admitted);
 // The transport admission window is not the pixel TTL. A low visual TTL
 // remains a shader decision; admission neither refreshes nor clamps age.
 BbNR::Config shortTTL;shortTTL.ttl_frames=1;shortTTL.ttl_ms=1.f;
 boundary.clocks[4].ms=200.;const auto retained=boundary.Admit();assert(retained.admitted);
 assert(boundary.current.source-boundary.links.front().frame.source>shortTTL.ttl_frames);
 assert(retained.times[4]-retained.times[0]>shortTTL.ttl_ms);
}
} // namespace

int main(){
 RequestRolloverKeepsVisualHistory();
 RequestBoundariesAndLineage();
 ObservedIdentityAndResetFloor();
 ClockFailuresAndClosedTimeBoundary();
 puts("PASS production candidate admission: lineage, visual identity, reset floor, clocks, independent TTL");
}
