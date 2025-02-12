/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2022, The Regents of the University of California
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "ord/Design.h"

#include <tcl.h>

#include "ant/AntennaChecker.hh"
#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "grt/GlobalRouter.h"
#include "ifp/InitFloorplan.hh"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "ord/Tech.h"
#include "sta/Corner.hh"
#include "sta/TimingArc.hh"
#include "sta/TimingRole.hh"
#include "utl/Logger.h"

namespace ord {

Design::Design(Tech* tech) : tech_(tech)
{
}

odb::dbBlock* Design::getBlock()
{
  auto chip = tech_->getDB()->getChip();
  return chip ? chip->getBlock() : nullptr;
}

void Design::readVerilog(const std::string& file_name)
{
  auto chip = tech_->getDB()->getChip();
  if (chip && chip->getBlock()) {
    getLogger()->error(utl::ORD, 36, "A block already exists in the db");
  }

  auto app = OpenRoad::openRoad();
  app->readVerilog(file_name.c_str());
}

void Design::readDef(const std::string& file_name,
                     bool continue_on_errors,  // = false
                     bool floorplan_init,      // = false
                     bool incremental,         // = false
                     bool child                // = false
)
{
  auto app = OpenRoad::openRoad();
  if (floorplan_init && incremental) {
    getLogger()->error(utl::ORD,
                       101,
                       "Only one of the options -incremental and"
                       " -floorplan_init can be set at a time");
  }
  if (tech_->getDB()->getTech() == nullptr) {
    getLogger()->error(utl::ORD, 102, "No technology has been read.");
  }
  app->readDef(file_name.c_str(),
               tech_->getDB()->getTech(),
               continue_on_errors,
               floorplan_init,
               incremental,
               child);
}

float Design::slew_corner(sta::Vertex *vertex)                            
{

  sta::Sta *sta = sta::Sta::sta();
  
  float slew_max = -sta::INF;

  for (auto corner : getCorners()) {
      slew_max = std::max(slew_max,  sta::delayAsFloat(sta->vertexSlew(vertex,  sta::RiseFall::rise(), corner, sta::MinMax::max())));
  }

  return slew_max;
}


float Design::getPinSlew(odb::dbITerm* db_pin) {
    const int num_vertex_elements = 2;

    sta::dbSta* sta = getSta();

    sta::Pin* sta_pin = sta->getDbNetwork()->dbToSta(db_pin);

    std::array<sta::Vertex*, 2> vertex_array = vertices(sta_pin);

    float pinSlew = -sta::INF;
    for (int i = 0; i < num_vertex_elements; i++) {
      sta::Vertex* vertex = vertex_array[i];
      if(vertex != nullptr){
        float pinSlewTemp = slew_corner(vertex);
        pinSlew = std::max(pinSlew,  pinSlewTemp);
      }
    }

    return pinSlew;
}


sta::Network* Design::cmdLinkedNetwork()
{
  sta::Network *network = sta::Sta::sta()->cmdNetwork();;
  if (network->isLinked()) {
    return network;
  }

  getLogger()->error(utl::ORD, 104, "STA network is not linked.");
}

sta::Graph* Design::cmdGraph()
{
  cmdLinkedNetwork();
  return sta::Sta::sta()->ensureGraph();
}

std::array<sta::Vertex*, 2> Design::vertices(const sta::Pin *pin)
{
  sta::Vertex *vertex, *vertex_bidirect_drvr;
  std::array<sta::Vertex*, 2> vertices;

  cmdGraph()->pinVertices(pin, vertex, vertex_bidirect_drvr);
  vertices[0] = vertex;
  vertices[1] = vertex_bidirect_drvr;
  return vertices;
}

std::vector<float> Design::arrivalsClk(const sta::RiseFall *rf,
	                                      sta::Clock *clk,
	                                      const sta::RiseFall *clk_rf,
                                        sta::Vertex *vertex)
{
  sta::Sta *sta = sta::Sta::sta();
  std::vector<float> arrivals;
  
  const sta::ClockEdge *clk_edge = nullptr;

  if (clk){
    clk_edge = clk->edge(clk_rf);
  }

  for (auto path_ap : sta->corners()->pathAnalysisPts()) {
    arrivals.push_back(sta::delayAsFloat(sta->vertexArrival(vertex, rf, clk_edge, path_ap, nullptr)));
  }
  return arrivals;
}

std::string Design::getITermName (odb::dbITerm* ITerm)
{
    auto MTerm_name = ITerm->getMTerm()->getName();
    auto inst_name  = ITerm->getInst()->getName();
    return inst_name + "/" + MTerm_name;
}

bool Design::isTimeInf(float time) {
  return (time > 1e+10 || time < -1e+10); 
}

float Design::getPinArrivalTime(sta::Clock *clk,
                               const sta::RiseFall *clk_rf,
                               sta::Vertex *vertex,
                               const std::string& arrrive_or_hold)
{
  const sta::RiseFall *rf = (arrrive_or_hold == "arrive")? sta::RiseFall::rise(): sta::RiseFall::fall();
  std::vector<float> times = arrivalsClk(rf, clk, clk_rf, vertex);
  float delay = -sta::INF;
  for (float delay_time : times){ 
    if(!isTimeInf(delay_time)) {
      delay = std::max(delay, delay_time);
    }
  }
  return delay;
}

sta::ClockSeq Design::findClocksMatching(const char *pattern,
		                                  bool regexp,
		                                  bool nocase)
{
  sta::Sta *sta = sta::Sta::sta();
  cmdLinkedNetwork();
  sta::PatternMatch matcher(pattern, regexp, nocase, sta->tclInterp());
  return sta::Sta::sta()->sdc()->findClocksMatching(&matcher);
}

sta::Clock* Design::defaultArrivalClock()
{
  return sta::Sta::sta()->sdc()->defaultArrivalClock();
}

float Design::getPinArrival(odb::dbITerm* db_pin, const std::string& rf) {
    std::vector<float> pin_arr;
    const int num_vertex_elements = 2;

    sta::dbSta* sta = getSta();
    sta::Pin* sta_pin = sta->getDbNetwork()->dbToSta(db_pin);

    std::array<sta::Vertex*, 2> vertex_arrray = vertices(sta_pin);
    float delay = -1;
    for (int i = 0; i < num_vertex_elements; i++) {
      sta::Vertex* vertex = vertex_arrray[i];
      if (vertex != nullptr) {
        std::string arrival_or_hold = (rf == "rise")? "arrive":"hold";
        delay = std::max(delay, getPinArrivalTime(nullptr, sta::RiseFall::rise(), vertex, arrival_or_hold));
        delay = std::max(delay, getPinArrivalTime(defaultArrivalClock(), sta::RiseFall::rise(), vertex, arrival_or_hold));
        for (auto clk : findClocksMatching("*", false, false)) {
          delay = std::max(delay, getPinArrivalTime(clk, sta::RiseFall::rise(), vertex, arrival_or_hold));
          delay = std::max(delay, getPinArrivalTime(clk, sta::RiseFall::fall(), vertex, arrival_or_hold));
        }
      }
    }
    return delay;
}

void Design::link(const std::string& design_name)
{
  auto app = OpenRoad::openRoad();
  app->linkDesign(design_name.c_str());
}

void Design::readDb(const std::string& file_name)
{
  auto app = OpenRoad::openRoad();
  app->readDb(file_name.c_str());
}

void Design::writeDb(const std::string& file_name)
{
  auto app = OpenRoad::openRoad();
  app->writeDb(file_name.c_str());
}

void Design::writeDef(const std::string& file_name)
{
  auto app = OpenRoad::openRoad();
  app->writeDef(file_name.c_str(), "5.8");
}

ifp::InitFloorplan* Design::getFloorplan()
{
  auto app = OpenRoad::openRoad();
  auto block = getBlock();
  if (!block) {
    getLogger()->error(utl::ORD, 37, "No block loaded.");
  }
  return new ifp::InitFloorplan(block, app->getLogger(), app->getDbNetwork());
}

utl::Logger* Design::getLogger()
{
  auto app = OpenRoad::openRoad();
  return app->getLogger();
}

int Design::micronToDBU(double coord)
{
  int dbuPerMicron = getBlock()->getDbUnitsPerMicron();
  return round(coord * dbuPerMicron);
}

ant::AntennaChecker* Design::getAntennaChecker()
{
  auto app = OpenRoad::openRoad();
  return app->getAntennaChecker();
}

const std::string Design::evalTclString(const std::string& cmd)
{
  Tcl_Interp* tcl_interp = OpenRoad::openRoad()->tclInterp();
  Tcl_Eval(tcl_interp, cmd.c_str());
  return std::string(Tcl_GetStringResult(tcl_interp));
}

Tech* Design::getTech()
{
  return tech_;
}

sta::dbSta* Design::getSta()
{
  auto app = OpenRoad::openRoad();
  return app->getSta();
}

std::vector<sta::Corner*> Design::getCorners()
{
  sta::Corners* corners = getSta()->corners();
  return {corners->begin(), corners->end()};
}

sta::MinMax* Design::getMinMax(MinMax type)
{
  return type == Max ? sta::MinMax::max() : sta::MinMax::min();
}

float Design::getNetCap(odb::dbNet* net, sta::Corner* corner, MinMax minmax)
{
  sta::dbSta* sta = getSta();
  sta::Net* sta_net = sta->getDbNetwork()->dbToSta(net);

  float pin_cap;
  float wire_cap;
  sta->connectedCap(sta_net, corner, getMinMax(minmax), pin_cap, wire_cap);
  return pin_cap + wire_cap;
}

sta::LibertyCell* Design::getLibertyCell(odb::dbMaster* master)
{
  sta::dbSta* sta = getSta();
  sta::dbNetwork* network = sta->getDbNetwork();

  sta::Cell* cell = network->dbToSta(master);
  if (!cell) {
    return nullptr;
  }
  return network->libertyCell(cell);
}

bool Design::isBuffer(odb::dbMaster* master)
{
  auto lib_cell = getLibertyCell(master);
  if (!lib_cell) {
    return false;
  }
  return lib_cell->isBuffer();
}

bool Design::isInverter(odb::dbMaster* master)
{
  auto lib_cell = getLibertyCell(master);
  if (!lib_cell) {
    return false;
  }
  return lib_cell->isInverter();
}

bool Design::isSequential(odb::dbMaster* master)
{
  auto lib_cell = getLibertyCell(master);
  if (!lib_cell) {
    return false;
  }
  return lib_cell->hasSequentials();
}

float Design::staticPower(odb::dbInst* inst, sta::Corner* corner)
{
  sta::dbSta* sta = getSta();
  sta::dbNetwork* network = sta->getDbNetwork();

  sta::Instance* sta_inst = network->dbToSta(inst);
  if (!sta_inst) {
    return 0.0;
  }
  sta::PowerResult power = sta->power(sta_inst, corner);
  return power.leakage();
}

float Design::dynamicPower(odb::dbInst* inst, sta::Corner* corner)
{
  sta::dbSta* sta = getSta();
  sta::dbNetwork* network = sta->getDbNetwork();

  sta::Instance* sta_inst = network->dbToSta(inst);
  if (!sta_inst) {
    return 0.0;
  }
  sta::PowerResult power = sta->power(sta_inst, corner);
  return (power.internal() + power.switching());
}

bool Design::isInClock(odb::dbInst* inst)
{
  for (auto* iterm : inst->getITerms()) {
    auto* net = iterm->getNet();
    if (net != nullptr && net->getSigType() == odb::dbSigType::CLOCK) {
      return true;
    }
  }
  return false;
}

bool Design::isInPower(odb::dbITerm* iterm)
{
  auto* net = iterm->getNet();
  return (net != nullptr && net->getSigType() == odb::dbSigType::POWER);
}

bool Design::isInGround(odb::dbITerm* iterm)
{
  auto* net = iterm->getNet();
  return (net != nullptr && net->getSigType() == odb::dbSigType::GROUND);
}

std::uint64_t Design::getNetRoutedLength(odb::dbNet* net)
{
  std::uint64_t route_length = 0;
  if (net->getSigType().isSupply()) {
    for (odb::dbSWire* swire : net->getSWires()) {
      for (odb::dbSBox* wire : swire->getWires()) {
        if (wire != nullptr && !(wire->isVia())) {
          route_length += wire->getLength();
        }
      }
    }
  } else {
    auto* wire = net->getWire();
    if (wire != nullptr) {
      route_length += wire->getLength();
    }
  }
  return route_length;
}

// I'd like to return a std::set but swig gave me way too much grief
// so I just copy the set to a vector.
std::vector<odb::dbMTerm*> Design::getTimingFanoutFrom(odb::dbMTerm* input)
{
  sta::dbSta* sta = getSta();
  sta::dbNetwork* network = sta->getDbNetwork();

  odb::dbMaster* master = input->getMaster();
  sta::Cell* cell = network->dbToSta(master);
  if (!cell) {
    return {};
  }

  sta::LibertyCell* lib_cell = network->libertyCell(cell);
  if (!lib_cell) {
    return {};
  }

  sta::Port* port = network->dbToSta(input);
  sta::LibertyPort* lib_port = network->libertyPort(port);

  std::set<odb::dbMTerm*> outputs;
  for (auto arc_set : lib_cell->timingArcSets(lib_port, /* to */ nullptr)) {
    sta::TimingRole* role = arc_set->role();
    if (role->isTimingCheck() || role->isAsyncTimingCheck()
        || role->isNonSeqTimingCheck() || role->isDataCheck()) {
      continue;
    }
    sta::LibertyPort* to_port = arc_set->to();
    odb::dbMTerm* to_mterm = master->findMTerm(to_port->name());
    if (to_mterm) {
      outputs.insert(to_mterm);
    }
  }
  return {outputs.begin(), outputs.end()};
}

grt::GlobalRouter* Design::getGlobalRouter()
{
  auto app = OpenRoad::openRoad();
  return app->getGlobalRouter();
}

gpl::Replace* Design::getReplace()
{
  auto app = OpenRoad::openRoad();
  return app->getReplace();
}

dpl::Opendp* Design::getOpendp()
{
  auto app = OpenRoad::openRoad();
  return app->getOpendp();
}

mpl::MacroPlacer* Design::getMacroPlacer()
{
  auto app = OpenRoad::openRoad();
  return app->getMacroPlacer();
}

ppl::IOPlacer* Design::getIOPlacer()
{
  auto app = OpenRoad::openRoad();
  return app->getIOPlacer();
}

tap::Tapcell* Design::getTapcell()
{
  auto app = OpenRoad::openRoad();
  return app->getTapcell();
}

cts::TritonCTS* Design::getTritonCts()
{
  auto app = OpenRoad::openRoad();
  return app->getTritonCts();
}

triton_route::TritonRoute* Design::getTritonRoute()
{
  auto app = OpenRoad::openRoad();
  return app->getTritonRoute();
}

dpo::Optdp* Design::getOptdp()
{
  auto app = OpenRoad::openRoad();
  return app->getOptdp();
}

fin::Finale* Design::getFinale()
{
  auto app = OpenRoad::openRoad();
  return app->getFinale();
}

par::PartitionMgr* Design::getPartitionMgr()
{
  auto app = OpenRoad::openRoad();
  return app->getPartitionMgr();
}

rcx::Ext* Design::getOpenRCX()
{
  auto app = OpenRoad::openRoad();
  return app->getOpenRCX();
}

rmp::Restructure* Design::getRestructure()
{
  auto app = OpenRoad::openRoad();
  return app->getRestructure();
}

stt::SteinerTreeBuilder* Design::getSteinerTreeBuilder()
{
  auto app = OpenRoad::openRoad();
  return app->getSteinerTreeBuilder();
}

psm::PDNSim* Design::getPDNSim()
{
  auto app = OpenRoad::openRoad();
  return app->getPDNSim();
}

pdn::PdnGen* Design::getPdnGen()
{
  auto app = OpenRoad::openRoad();
  return app->getPdnGen();
}

pad::ICeWall* Design::getICeWall()
{
  auto app = OpenRoad::openRoad();
  return app->getICeWall();
}

}  // namespace ord
