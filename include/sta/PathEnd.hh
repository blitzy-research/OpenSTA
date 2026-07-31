// OpenSTA, Static Timing Analyzer
// Copyright (c) 2026, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
// 
// The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software.
// 
// Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 
// This notice may not be removed or altered from any source distribution.

#pragma once

#include <string>

#include "GraphClass.hh"
#include "LibertyClass.hh"
#include "Path.hh"
#include "SdcClass.hh"
#include "SearchClass.hh"
#include "StaState.hh"

namespace sta {

class StaState;
class RiseFall;
class MinMax;
class ReportPath;

// PathEnds represent search endpoints that are either unconstrained
// or constrained by a timing check, output delay, data check,
// or path delay.
//
// Class hierarchy:
// PathEnd (abstract)
//  PathEndUnconstrained
//  PathEndClkConstrained (abstract)
//   PathEndPathDelay (clock is optional)
//   PathEndClkConstrainedMcp (abstract)
//    PathEndCheck
//     PathEndLatchCheck
//    PathEndOutputDelay
//    PathEndGatedClock
//    PathEndDataCheck
//
// Which check or constraint each concrete type models, taken from each type's
// check role and constraint behavior in search/PathEnd.cc.  Six of the seven
// rows below come from a checkRole() override.  PathEndUnconstrained has none,
// so its row is taken from its required time, margin and slack instead, and
// the checkRole() it inherits returns null (search/PathEnd.cc:L225-L229).
//  PathEndUnconstrained - no constraint applies; the required time is the
//    initial value for the opposite min/max sense, the margin is zero, and the
//    slack is infinite (search/PathEnd.cc:L468-L490).
//  PathEndCheck - setup, hold, recovery or removal; the role is the check
//    edge's role, so all four are this one class (search/PathEnd.cc:L961-L965).
//  PathEndLatchCheck - latch D input setup; setup() for a pulse clock and
//    latchSetup() otherwise (search/PathEnd.cc:L1155-L1165).
//  PathEndOutputDelay - set_output_delay; outputSetup() for max and
//    outputHold() for min (search/PathEnd.cc:L1360-L1367).
//  PathEndGatedClock - gated clock check; the role is supplied at construction
//    (search/PathEnd.cc:L1523-L1527).
//  PathEndDataCheck - set_data_check; dataCheckSetup() for max and
//    dataCheckHold() for min (search/PathEnd.cc:L1668-L1675).
//  PathEndPathDelay - set_min_delay/set_max_delay; the check edge's role when
//    the path delay ends at a timing check, otherwise setup for max and hold
//    for min (search/PathEnd.cc:L1799-L1808).
//
// Which of those types is built for a given endpoint is decided by the factory
// in search/VisitPathEnds.cc, not by anything declared here.  search/PathEnd.md
// documents the family at length: the search and reporting pipeline it sits in,
// the responsibility split across the clock constrained chain, latch time
// borrowing, the comparators, the member field inventory, and the invariants a
// maintainer must respect.
//
// PathEnd itself is abstract and is never instantiated: copy(), type(),
// typeName(), reportShort(), reportFull(), requiredTime(), margin(), slack(),
// slackNoCrpr() and sourceClkOffset() are all pure virtual, and the constructor
// is protected.  What this class contributes is the single contract every
// endpoint answers - arrival, required time, margin, slack, crpr and the target
// clock accessors - so that reporting and path grouping can treat a constrained
// and an unconstrained endpoint identically.
//
// Lifetime: the endpoints handed to a PathEndVisitor are stack temporaries
// inside the factory, so anything that keeps an endpoint past the visit
// callback must take a copy() of it first (search/PathGroup.cc:L164).
class PathEnd
{
public:
  // Exactly one enumerator per concrete subclass, seven of each; the three
  // abstract classes have none.  This order is semantics, not presentation:
  // exceptPathCmp compares these enumerators as ordinals, so the order written
  // here decides how endpoints of different kinds sort against each other
  // (search/PathEnd.cc:L282-L294).  The ordinal values are additionally
  // asserted by TEST_F(StaInitTest, PathEndTypeValues) in
  // search/test/cpp/TestSearchStaInit.cc:L1632-L1641, and dispatch outside the
  // reporting path branches on them (search/Sta.cc:L3481-L3482), so reordering
  // this list changes ordering behavior and breaks a committed test.
  // It is its own order: it matches neither the hierarchy listed above nor the
  // order of the reporting overloads (search/ReportPath.hh:L92-L106).
  enum class Type { unconstrained,
                    check,
                    data_check,
                    latch_check,
                    output_delay,
                    gated_clk,
                    path_delay
  };

  virtual PathEnd *copy() const = 0;
  virtual ~PathEnd() = default;
  // Observation: this member is declared here and defined nowhere in the
  // repository.  The unit tests record the same finding
  // (search/test/cpp/TestSearchStaInit.cc:L3861).
  void deletePath();
  Path *path() { return path_; }
  const Path *path() const { return path_; }
  virtual void setPath(Path *path);
  Vertex *vertex(const StaState *sta) const;
  const MinMax *minMax(const StaState *sta) const;
  // Synonym for minMax().
  const EarlyLate *pathEarlyLate(const StaState *sta) const;
  virtual const EarlyLate *clkEarlyLate(const StaState *sta) const;
  const RiseFall *transition(const StaState *sta) const;
  virtual void reportShort(const ReportPath *report) const = 0;
  virtual void reportFull(const ReportPath *report) const = 0;
  PathGroup *pathGroup() const { return path_group_; }
  void setPathGroup(PathGroup *path_group);

  // Predicates for PathEnd type.
  // Default methods overridden by respective types.
  // Each predicate names one concrete leaf type, so these are not is-a tests
  // and they do not follow the inheritance chain.  isCheck() is the case that
  // shows it: PathEndCheck overrides it to true, and PathEndLatchCheck, which
  // derives from PathEndCheck, overrides it back to false and answers
  // isLatchCheck() instead.  Code that means "any timing check" therefore has
  // to test both, as path grouping does (search/PathGroup.cc:L446, L496).
  [[nodiscard]] virtual bool isUnconstrained() const { return false; }
  [[nodiscard]] virtual bool isCheck() const { return false; }
  [[nodiscard]] virtual bool isDataCheck() const { return false; }
  [[nodiscard]] virtual bool isLatchCheck() const { return false; }
  [[nodiscard]] virtual bool isOutputDelay() const { return false; }
  [[nodiscard]] virtual bool isGatedClock() const { return false; }
  [[nodiscard]] virtual bool isPathDelay() const { return false; }
  virtual Type type() const = 0;
  virtual const char *typeName() const = 0;
  virtual int exceptPathCmp(const PathEnd *path_end,
                            const StaState *sta) const;
  virtual const Arrival &dataArrivalTime(const StaState *sta) const;
  // Arrival time with source clock offset.
  Arrival dataArrivalTimeOffset(const StaState *sta) const;
  virtual Required requiredTime(const StaState *sta) const = 0;
  // Required time with source clock offset.
  virtual Required requiredTimeOffset(const StaState *sta) const;
  virtual ArcDelay margin(const StaState *sta) const = 0;
  virtual float macroClkTreeDelay(const StaState *) const { return 0.0; }
  virtual Slack slack(const StaState *sta) const = 0;
  virtual Slack slackNoCrpr(const StaState *sta) const = 0;
  virtual Arrival borrow(const StaState *sta) const;
  const ClockEdge *sourceClkEdge(const StaState *sta) const;
  // Time offset for the path start so the path begins in the correct
  // source cycle.
  virtual float sourceClkOffset(const StaState *sta) const = 0;
  virtual Delay sourceClkLatency(const StaState *sta) const;
  virtual Delay sourceClkInsertionDelay(const StaState *sta) const;
  virtual Path *targetClkPath();
  virtual const Path *targetClkPath() const;
  virtual const Clock *targetClk(const StaState *sta) const;
  virtual const ClockEdge *targetClkEdge(const StaState *sta) const;
  const RiseFall *targetClkEndTrans(const StaState *sta) const;
  // Target clock with cycle accounting and source clock offsets.
  virtual float targetClkTime(const StaState *sta) const;
  // Time offset for the target clock.
  virtual float targetClkOffset(const StaState *sta) const;
  // Target clock with source clock offset.
  virtual Arrival targetClkArrival(const StaState *sta) const;
  // Target clock tree delay.
  virtual Delay targetClkDelay(const StaState *sta) const;
  virtual Delay targetClkInsertionDelay(const StaState *sta) const;
  // Does NOT include inter-clk uncertainty.
  virtual float targetNonInterClkUncertainty(const StaState *sta) const;
  virtual float interClkUncertainty(const StaState *sta) const;
  // Target clock uncertainty + inter-clk uncertainty.
  virtual float targetClkUncertainty(const StaState *sta) const;
  virtual float targetClkMcpAdjustment(const StaState *sta) const;
  virtual const TimingRole *checkRole(const StaState *sta) const;
  const TimingRole *checkGenericRole(const StaState *sta) const;
  virtual bool pathDelayMarginIsExternal() const;
  virtual PathDelay *pathDelay() const;
  // This returns the crpr signed with respect to the check type.
  // Positive for setup, negative for hold.
  virtual Crpr checkCrpr(const StaState *sta) const;
  virtual Crpr crpr(const StaState *sta) const;
  virtual MultiCyclePath *multiCyclePath() const;
  virtual TimingArc *checkArc() const { return nullptr; }
  // PathEndDataCheck data clock path.
  virtual const Path *dataClkPath() const { return nullptr; }
  virtual Delay clkSkew(const StaState *sta);
  [[nodiscard]] virtual bool ignoreClkLatency(const StaState *) const { return false; }

  static bool less(const PathEnd *path_end1,
                   const PathEnd *path_end2,
                   // Compare slack (if constrained), or arrival when false.
                   bool cmp_slack,
                   const StaState *sta);
  static int cmp(const PathEnd *path_end1,
                 const PathEnd *path_end2,
                 // Compare slack (if constrained), or arrival when false.
                 bool cmp_slack,
                 const StaState *sta);
  static int cmpSlack(const PathEnd *path_end1,
                      const PathEnd *path_end2,
                      const StaState *sta);
  static int cmpArrival(const PathEnd *path_end1,
                        const PathEnd *path_end2,
                        const StaState *sta);
  static int cmpNoCrpr(const PathEnd *path_end1,
                       const PathEnd *path_end2,
                       const StaState *sta);

  // Helper common to multiple PathEnd classes and used
  // externally.
  // Target clock insertion delay + latency.
  static Delay checkTgtClkDelay(const Path *tgt_clk_path,
                                const ClockEdge *tgt_clk_edge,
                                const TimingRole *check_role,
                                const StaState *sta);
  static void checkTgtClkDelay(const Path *tgt_clk_path,
                               const ClockEdge *tgt_clk_edge,
                               const TimingRole *check_role,
                               const StaState *sta,
                               // Return values.
                               Delay &insertion,
                               Delay &latency);
  static float checkClkUncertainty(const ClockEdge *src_clk_edge,
                                   const ClockEdge *tgt_clk_edge,
                                   const Path *tgt_clk_path,
                                   const TimingRole *check_role,
                                   const Sdc *sdc);
  // Non inter-clock uncertainty.
  static float checkTgtClkUncertainty(const Path *tgt_clk_path,
                                      const ClockEdge *tgt_clk_edge,
                                      const TimingRole *check_role,
                                      const StaState *sta);
  static float checkSetupMcpAdjustment(const ClockEdge *src_clk_edge,
                                       const ClockEdge *tgt_clk_edge,
                                       const MultiCyclePath *mcp,
                                       int default_cycles,
                                       Sdc *sdc);

protected:
  PathEnd(Path *path);
  static void checkInterClkUncertainty(const ClockEdge *src_clk_edge,
                                       const ClockEdge *tgt_clk_edge,
                                       const TimingRole *check_role,
                                       const Sdc *sdc,
                                       float &uncertainty,
                                       bool &exists);
  static float outputDelayMargin(OutputDelay *output_delay,
                                 const Path *path,
                                 const StaState *sta);
  static float pathDelaySrcClkOffset(const Path *path,
                                     PathDelay *path_delay,
                                     Arrival src_clk_arrival,
                                     const StaState *sta);
  static bool ignoreClkLatency(const Path *path,
                               PathDelay *path_delay,
                               const StaState *sta);
  virtual int setupDefaultCycles() const { return 1; }

  // The data path this endpoint terminates.  Everything the family reports
  // about the data side is read back out of it, each of those accessors simply
  // forwarding to it: the vertex and the min/max sense
  // (search/PathEnd.cc:L68-L78), the transition and the source clock edge
  // (search/PathEnd.cc:L92-L102), and the arrival time, which dataArrivalTime()
  // returns as path_->arrival() (search/PathEnd.cc:L104-L108).
  Path *path_;
  // The reporting bucket this endpoint was sorted into.  It is filled in by
  // path grouping through setPathGroup() rather than at construction, so it is
  // null on an endpoint that has not yet been grouped
  // (search/PathGroup.cc:L182).
  PathGroup *path_group_{nullptr};
};

// The endpoint that no constraint reaches.  It exists so that an unconstrained
// endpoint can be reported through the same interface as a constrained one
// instead of being a special case in the reporting code, and it answers that
// contract with neutral values: required time is the initial value for the
// opposite min/max sense, the margin is zero, and the slack is infinite
// (search/PathEnd.cc:L468-L490).  It derives straight from PathEnd rather than
// from PathEndClkConstrained, so it is the only concrete type that keeps the
// base target clock accessors, which return null or zero without consulting
// anything (search/PathEnd.cc:L159-L223, L231-L241).  That is not the same as
// being the only concrete type that can lack a target clock: PathEndPathDelay
// overrides targetClkEdge() and still returns null when it has neither a clock
// path nor an output delay (search/PathEnd.cc:L1858-L1867).  Both
// comparators special case it, ordering it by negated arrival rather than by
// its infinite slack (search/PathEnd.cc:L1980-L1982, L2091-L2093).
class PathEndUnconstrained : public PathEnd
{
public:
  PathEndUnconstrained(Path *path);
  Type type() const override;
  const char *typeName() const override;
  PathEnd *copy() const override;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  bool isUnconstrained() const override;
  Required requiredTime(const StaState *sta) const override;
  Required requiredTimeOffset(const StaState *sta) const override;
  ArcDelay margin(const StaState *sta) const override;
  Slack slack(const StaState *sta) const override;
  Slack slackNoCrpr(const StaState *sta) const override;
  float sourceClkOffset(const StaState *sta) const override;
};

// Abstract; the protected constructor keeps it from being instantiated.
// It is the base of every endpoint except PathEndUnconstrained, and it exists
// to own the two things they share by default.  The first is state: the target
// clock path, plus a lazily cached crpr value.  The second is algebra: the
// required time before crpr is the target clock arrival adjusted by the margin,
// subtracting it for setup and adding it for hold; the check crpr is then added
// on; and slack is required minus arrival for setup and arrival minus required
// for hold (search/PathEnd.cc:L706-L734).  Most of what a leaf type adds on top
// of that is just its own margin and check role.
//
// Both halves are inherited defaults rather than universals.  Every leaf type
// except PathEndGatedClock replaces part of the algebra by overriding
// requiredTime, requiredTimeNoCrpr or targetClkArrivalNoCrpr, and four of them
// also change where the target clock or the crpr comes from, so read the target
// clock accessors declared below as the default behavior rather than as a
// guarantee:
//  PathEndOutputDelay - when clk_path_ is null the target clock edge comes from
//    the output delay itself (search/PathEnd.cc:L1369-L1376), and crpr() refills
//    the inherited cache from that edge instead (search/PathEnd.cc:L1397-L1405).
//  PathEndDataCheck - the target clock edge comes from the related data clock
//    path, because what it constrains is one data pin against another
//    (search/PathEnd.cc:L1625-L1630).
//  PathEndLatchCheck - clk_path_ carries the latch enable, and the target clock
//    time and offset are overridden for the path delay case
//    (search/PathEnd.cc:L1167-L1183).
//  PathEndPathDelay - there may be no target clock at all
//    (search/PathEnd.cc:L1858-L1867).
class PathEndClkConstrained : public PathEnd
{
public:
  float sourceClkOffset(const StaState *sta) const override;
  Delay sourceClkLatency(const StaState *sta) const override;
  Delay sourceClkInsertionDelay(const StaState *sta) const override;
  const Clock *targetClk(const StaState *sta) const override;
  const ClockEdge *targetClkEdge(const StaState *sta) const override;
  Path *targetClkPath() override;
  const Path *targetClkPath() const override;
  float targetClkTime(const StaState *sta) const override;
  float targetClkOffset(const StaState *sta) const override;
  Arrival targetClkArrival(const StaState *sta) const override;
  Delay targetClkDelay(const StaState *sta) const override;
  Delay targetClkInsertionDelay(const StaState *sta) const override;
  float targetNonInterClkUncertainty(const StaState *sta) const override;
  float interClkUncertainty(const StaState *sta) const override;
  float targetClkUncertainty(const StaState *sta) const override;
  Crpr crpr(const StaState *sta) const override;
  Required requiredTime(const StaState *sta) const override;
  Slack slack(const StaState *sta) const override;
  Slack slackNoCrpr(const StaState *sta) const override;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;
  // Replaces the data path and also invalidates the cached crpr value, because
  // that value was computed for the previous data path.  This is the only place
  // crpr_valid_ is cleared (search/PathEnd.cc:L521-L526), which is what keeps a
  // reported crpr matched to the path it was computed for.
  void setPath(Path *path) override;

protected:
  PathEndClkConstrained(Path *path,
                        Path *clk_path);
  float sourceClkOffset(const ClockEdge *src_clk_edge,
                        const ClockEdge *tgt_clk_edge,
                        const TimingRole *check_role,
                        const StaState *sta) const;
  // Internal to slackNoCrpr.
  virtual Arrival targetClkArrivalNoCrpr(const StaState *sta) const;
  virtual Required requiredTimeNoCrpr(const StaState *sta) const;

  // The target clock path, and by default the source of the target clock, its
  // edge, the clock tree delay, and the vertex the check arc is derated against
  // (search/PathEnd.cc:L967-L975).  What it holds is narrower in the leaf types
  // that the class comment above lists: the latch enable in PathEndLatchCheck,
  // a value derived from the data clock path that may stay null in
  // PathEndDataCheck, and only the first of the possible sources of the target
  // clock edge in PathEndOutputDelay and PathEndPathDelay.
  Path *clk_path_;
  // Lazily filled crpr cache.  Both members are mutable so that the const
  // accessor can fill them on first use, and the inherited fill keys the value
  // on path_ and targetClkPath() (search/PathEnd.cc:L695-L704).  It is not
  // always keyed that way: PathEndOutputDelay::crpr() fills these same two
  // members from path_ and the target clock edge instead
  // (search/PathEnd.cc:L1397-L1405).  For either fill, setPath() declared above
  // is the only thing that clears crpr_valid_.
  mutable Crpr crpr_;
  mutable bool crpr_valid_{false};
};

// Abstract; the protected constructor keeps it from being instantiated.
// It adds exactly one thing to PathEndClkConstrained and nothing else: the
// multicycle path and the target clock cycle adjustment derived from it.  Its
// constructor stores mcp_ and does no other work
// (search/PathEnd.cc:L753-L759).  Keeping the layer this thin is what lets a
// type opt out of multicycle handling simply by deriving from
// PathEndClkConstrained instead, which is what PathEndPathDelay does.
class PathEndClkConstrainedMcp : public PathEndClkConstrained
{
public:
  MultiCyclePath *multiCyclePath() const override { return mcp_; }
  float targetClkMcpAdjustment(const StaState *sta) const override;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;

protected:
  PathEndClkConstrainedMcp(Path *path,
                           Path *clk_path,
                           MultiCyclePath *mcp);
  float checkMcpAdjustment(const Path *path,
                           const ClockEdge *tgt_clk_edge,
                           const StaState *sta) const;
  void findHoldMcps(const ClockEdge *tgt_clk_edge,
                    const MultiCyclePath *&setup_mcp,
                    const MultiCyclePath *&hold_mcp,
                    const StaState *sta) const;

  // The multicycle exception that shifts the target clock cycle.  It is null
  // when no multicycle path applies, and consumers test for that through
  // multiCyclePath() above (search/Sta.cc:L3487).
  MultiCyclePath *mcp_;
};

// Path constrained by timing check.
// One class covers setup, hold, recovery and removal.  The kind of check is
// not encoded in the type at all: checkRole() returns the role of the check
// edge, so it is resolved at run time from the graph
// (search/PathEnd.cc:L961-L965), and margin() derates the check arc for the
// path's min/max sense and analysis point (search/PathEnd.cc:L967-L975).  That
// is why all four report the same type name, "check"
// (search/PathEnd.cc:L946), and why telling them apart means asking the role
// rather than asking the type.
class PathEndCheck : public PathEndClkConstrainedMcp
{
public:
  PathEndCheck(Path *path,
               TimingArc *check_arc,
               Edge *check_edge,
               Path *clk_path,
               MultiCyclePath *mcp,
               const StaState *sta);
  PathEnd *copy() const override;
  Type type() const override;
  const char *typeName() const override;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  bool isCheck() const override { return true; }
  ArcDelay margin(const StaState *sta) const override;
  float macroClkTreeDelay(const StaState *sta) const override;
  const TimingRole *checkRole(const StaState *sta) const override;
  TimingArc *checkArc() const override { return check_arc_; }
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;
  Delay clkSkew(const StaState *sta) override;

protected:
  Delay sourceClkDelay(const StaState *sta) const;
  Required requiredTimeNoCrpr(const StaState *sta) const override;

  // The check arc and its edge are the source of both numbers this type
  // reports: margin() derates the arc (search/PathEnd.cc:L967-L975) and
  // checkRole() returns the edge's role (search/PathEnd.cc:L961-L965).  They
  // are what makes one class enough for four kinds of check.
  TimingArc *check_arc_;
  Edge *check_edge_;
};

// PathEndClkConstrained::clk_path_ is the latch enable.
// The enable is not passed in.  The constructor hands null to PathEndCheck and
// then assigns clk_path_ from the disable path through
// Latches::latchEnableOtherPath (search/PathEnd.cc:L1080-L1099).  Deriving
// clk_path_ in the constructor rather than taking it from the caller is not
// what makes this type different - PathEndDataCheck also passes null to its
// base and then derives its own (search/PathEnd.cc:L1563-L1573).  What is
// specific here is what it is derived into: the enable path that opens the
// latch, obtained from the disable path that the setup check is made against.
// It is also the only type in the family that borrows time, and it implements
// none of the borrowing arithmetic: required time, borrow, and the borrow report
// are all answered by the latch service
// (search/PathEnd.cc:L1185-L1246).  Its check role is setup() for a pulse clock
// and latchSetup() otherwise, because latch setup cycle accounting is relative
// to the enable opening edge rather than to the disable edge that the check is
// made against (search/PathEnd.cc:L1155-L1165).
class PathEndLatchCheck : public PathEndCheck
{
public:
  PathEndLatchCheck(Path *path,
                    TimingArc *check_arc,
                    Edge *check_edge,
                    Path *disable_path,
                    MultiCyclePath *mcp,
                    PathDelay *path_delay,
                    const StaState *sta);
  Type type() const override;
  const char *typeName() const override;
  float sourceClkOffset(const StaState *sta) const override;
  bool isCheck() const override { return false; }
  bool isLatchCheck() const override { return true; }
  PathDelay *pathDelay() const override { return path_delay_; }
  PathEnd *copy() const override;
  Path *latchDisable();
  const Path *latchDisable() const;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  const TimingRole *checkRole(const StaState *sta) const override;
  Required requiredTime(const StaState *sta) const override;
  // The only override of PathEnd::borrow in the family; every other type keeps
  // the base implementation, which returns zero (search/PathEnd.cc:L255-L259).
  // The amount is not computed here: it is one of the values returned by
  // Latches::latchRequired (search/Latches.hh:L52), which this forwards to
  // (search/PathEnd.cc:L1198-L1209).
  Arrival borrow(const StaState *sta) const override;
  float targetClkTime(const StaState *sta) const override;
  float targetClkOffset(const StaState *sta) const override;
  Arrival targetClkWidth(const StaState *sta) const;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;
  void latchRequired(const StaState *sta,
                     // Return values.
                     Required &required,
                     Delay &borrow,
                     Arrival &adjusted_data_arrival,
                     Delay &time_given_to_startpoint) const;
  void latchBorrowInfo(const StaState *sta,
                       // Return values.
                       float &nom_pulse_width,
                       Delay &open_latency,
                       Delay &latency_diff,
                       float &open_uncertainty,
                       Crpr &open_crpr,
                       Crpr &crpr_diff,
                       Delay &max_borrow,
                       bool &borrow_limit_exists) const;
  bool ignoreClkLatency(const StaState *sta) const override;

protected:
  // The latch disable path, which the implementation calls the setup check edge
  // (search/PathEnd.cc:L1163).  targetClkWidth() measures the enable open pulse
  // as the disable arrival minus the enable arrival, adding a clock period when
  // the enable arrives after the disable (search/PathEnd.cc:L1248-L1267).
  Path *disable_path_;
  // Set when a set_min_delay or set_max_delay exception also applies to this
  // latch endpoint; it is handed to the latch service together with
  // src_clk_arrival_ (search/PathEnd.cc:L1191-L1194).
  PathDelay *path_delay_;
  // Source clk arrival for set_max_delay -ignore_clk_latency.
  Arrival src_clk_arrival_;
};

// Path constrained by an output delay.
// If there is a reference pin, clk_path_ is the reference pin clock.
// If there is a path delay PathEndPathDelay is used instead of this.
// The margin is not a library number: it is the set_output_delay value for the
// path transition and min/max sense, returned as is for max and negated for min
// (search/PathEnd.cc:L1346-L1358).  The check role follows the same sense,
// outputSetup() for max and outputHold() for min
// (search/PathEnd.cc:L1360-L1367), and those two roles are generically setup and
// hold, so requiredTimeNoCrpr subtracts the margin in the max case and adds it
// in the min case (search/PathEnd.cc:L714-L723).  The negation is what cancels
// that sign flip, leaving the output delay value subtracted from the target
// clock arrival in both senses.
// Observation: an implementation comment states there is no target clock path
// for output delays (search/PathEnd.cc:L1304), yet the constructor it precedes
// forwards clk_path to the base and targetClkArrivalNoCrpr() branches on
// clk_path_ being non-null (search/PathEnd.cc:L1381), so that comment and the
// reference pin line above disagree.
class PathEndOutputDelay : public PathEndClkConstrainedMcp
{
public:
  PathEndOutputDelay(OutputDelay *output_delay,
                     Path *path,
                     Path *clk_path,
                     MultiCyclePath *mcp,
                     const StaState *sta);
  PathEnd *copy() const override;
  Type type() const override;
  const char *typeName() const override;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  bool isOutputDelay() const override { return true; }
  ArcDelay margin(const StaState *sta) const override;
  const TimingRole *checkRole(const StaState *sta) const override;
  const ClockEdge *targetClkEdge(const StaState *sta) const override;
  Delay targetClkDelay(const StaState *sta) const override;
  Delay targetClkInsertionDelay(const StaState *sta) const override;
  Crpr crpr(const StaState *sta) const override;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;

protected:
  Arrival targetClkArrivalNoCrpr(const StaState *sta) const override;
  Arrival tgtClkDelay(const ClockEdge *tgt_clk_edge,
                      const TimingRole *check_role,
                      const StaState *sta) const;
  void tgtClkDelay(const ClockEdge *tgt_clk_edge,
                   const TimingRole *check_role,
                   const StaState *sta,
                   // Return values.
                   Arrival &insertion,
                   Arrival &latency) const;

  // The set_output_delay constraint that supplies the margin.
  OutputDelay *output_delay_;
};

// Clock path constrained clock gating signal.
// Alone in the family, this type is told its answers instead of deriving them:
// the check role and the margin are both computed by the caller and passed to
// the constructor, then returned verbatim by checkRole()
// (search/PathEnd.cc:L1523-L1527) and by the inline margin() below.  It holds
// no check arc and does not override checkArc(), so it keeps the null default
// declared in PathEnd: a gated clock check constrains an enable against its
// clock, and there is no library timing arc to derate.  Gated clock endpoints
// are produced only while gated clock checking is enabled
// (search/VisitPathEnds.cc:L125).
class PathEndGatedClock : public PathEndClkConstrainedMcp
{
public:
  PathEndGatedClock(Path *gating_ref,
                    Path *clk_path,
                    const TimingRole *check_role,
                    MultiCyclePath *mcp,
                    ArcDelay margin,
                    const StaState *sta);
  PathEnd *copy() const override;
  Type type() const override;
  const char *typeName() const override;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  bool isGatedClock() const override { return true; }
  ArcDelay margin(const StaState *) const override { return margin_; }
  const TimingRole *checkRole(const StaState *sta) const override;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;

protected:
  // Both are supplied at construction and returned unchanged, which is what
  // frees this type from needing a check arc or a min/max test to decide
  // either one.
  const TimingRole *check_role_;
  ArcDelay margin_;
};

// Path constrained by a set_data_check, which checks one data pin against
// another data pin rather than against a clock.  That is the distinction this
// type encodes: the reference is a data path, so the roles are the data check
// roles, dataCheckSetup() for max and dataCheckHold() for min
// (search/PathEnd.cc:L1668-L1675).  The inherited clk_path_ is not supplied by
// the caller; the constructor passes null to the base and then derives it from
// the related data clock path (search/PathEnd.cc:L1563-L1573), and it can stay
// null when that path comes from an input port (search/PathEnd.cc:L1628).
class PathEndDataCheck : public PathEndClkConstrainedMcp
{
public:
  PathEndDataCheck(DataCheck *check,
                   Path *data_path,
                   Path *data_clk_path,
                   MultiCyclePath *mcp,
                   const StaState *sta);
  PathEnd *copy() const override;
  Type type() const override;
  const char *typeName() const override;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  bool isDataCheck() const override { return true; }
  const ClockEdge *targetClkEdge(const StaState *sta) const override;
  const TimingRole *checkRole(const StaState *sta) const override;
  ArcDelay margin(const StaState *sta) const override;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;
  const Path *dataClkPath() const override { return data_clk_path_; }

protected:
  Path *clkPath(Path *path,
                const StaState *sta);
  Arrival requiredTimeNoCrpr(const StaState *sta) const override;
  // setup uses zero cycle default
  int setupDefaultCycles() const override { return 0; }

  // Path of the clock related to the checked data pin.  dataClkPath() exposes
  // it and targetClkEdge() takes the target clock edge from it rather than
  // from clk_path_ (search/PathEnd.cc:L1626-L1630).
  Path *data_clk_path_;
  // The set_data_check constraint that supplies the margin.
  DataCheck *check_;
};

// Path constrained by set_min/max_delay.
// "Clocked" when path delay ends at timing check pin.
// May end at output with set_output_delay.
// Note the parent: this is the only concrete type that derives from
// PathEndClkConstrained directly instead of going through
// PathEndClkConstrainedMcp, so it inherits no multicycle handling at all.  That
// omission is deliberate rather than accidental, and the factory says
// why in its own words: "False paths and path delays override multicycle
// paths." (search/VisitPathEnds.cc:L262).  Its check role is the check edge's
// role when the path delay ends at a timing check, and otherwise setup for max
// and hold for min (search/PathEnd.cc:L1799-L1808).
class PathEndPathDelay : public PathEndClkConstrained
{
public:
  // Vanilla path delay.
  PathEndPathDelay(PathDelay *path_delay,
                   Path *path,
                   const StaState *sta);
  // Path delay to timing check.
  PathEndPathDelay(PathDelay *path_delay,
                   Path *path,
                   Path *clk_path,
                   TimingArc *check_arc,
                   Edge *check_edge,
                   const StaState *sta);
  // Path delay to output with set_output_delay.
  PathEndPathDelay(PathDelay *path_delay,
                   Path *path,
                   OutputDelay *output_delay,
                   const StaState *sta);
  PathEnd *copy() const override;
  Type type() const override;
  const char *typeName() const override;
  void reportShort(const ReportPath *report) const override;
  void reportFull(const ReportPath *report) const override;
  bool isPathDelay() const override { return true; }
  const TimingRole *checkRole(const StaState *sta) const override;
  // True exactly when there is no check arc, that is when the path delay does
  // not end at a timing check and the margin therefore comes from the
  // exception rather than from a library check (search/PathEnd.cc:L1793-L1797).
  bool pathDelayMarginIsExternal() const override;
  PathDelay *pathDelay() const override { return path_delay_; }
  ArcDelay margin(const StaState *sta) const override;
  float sourceClkOffset(const StaState *sta) const override;
  const ClockEdge *targetClkEdge(const StaState *sta) const override;
  float targetClkTime(const StaState *sta) const override;
  float targetClkOffset(const StaState *sta) const override;
  TimingArc *checkArc() const override { return check_arc_; }
  Required requiredTime(const StaState *sta) const override;
  int exceptPathCmp(const PathEnd *path_end,
                    const StaState *sta) const override;
  [[nodiscard]] bool hasOutputDelay() const { return output_delay_ != nullptr; }
  bool ignoreClkLatency(const StaState *sta) const override;

protected:
  Arrival targetClkArrivalNoCrpr(const StaState *sta) const override;
  void findSrcClkArrival(const StaState *sta);

  // The set_min_delay or set_max_delay exception that constrains this endpoint.
  PathDelay *path_delay_;
  // Check arc and edge are both null unless the path delay ends at a timing
  // check pin; only the check taking constructor sets them
  // (search/PathEnd.cc:L1711-L1748).  Null is therefore meaningful here: it is
  // what pathDelayMarginIsExternal() tests.
  TimingArc *check_arc_;
  Edge *check_edge_;
  // Output delay is nullptr when there is no output delay at the endpoint.
  OutputDelay *output_delay_;
  // Source clk arrival for set_min/max_delay -ignore_clk_latency.
  Arrival src_clk_arrival_;
};

////////////////////////////////////////////////////////////////

// Compare slack or arrival for unconstrained path ends and pin names,
// transitions along the source path.
// This is the ordering reporting uses when equal slacks still have to be put in
// a definite sequence.  operator() holds no logic of its own; it forwards to
// PathEnd::less (the call is search/PathEnd.cc:L2075, in the body at
// search/PathEnd.cc:L2071-L2076), and PathEnd::less is itself a one line forward
// to PathEnd::cmp (search/PathEnd.cc:L1965-L1972).  PathEnd::cmp
// (search/PathEnd.cc:L1974-L1999) is where the ordering actually lives: it
// compares slack, or negated arrival when slack comparison is switched off or
// the first end is unconstrained (search/PathEnd.cc:L1980-L1982), and only then
// breaks the tie in four further steps: pin, transition and clock of the data
// path, the same for the target clock path, then a full comparison of each
// (search/PathEnd.cc:L1986, L1990, L1992, L1994).  Those four steps, and the
// fact that it honors cmp_slack_ at all, are what distinguish it from
// PathEndSlackLess.
class PathEndLess
{
public:
  PathEndLess(bool cmp_slack,
              const StaState *sta);
  bool operator()(const PathEnd *path_end1,
                  const PathEnd *path_end2) const;

protected:
  // Comparator configuration.  cmp_slack_ selects slack rather than arrival as
  // the primary key and is read at search/PathEnd.cc:L2075.
  bool cmp_slack_;
  const StaState *sta_;
};

// Compare slack or arrival for unconstrained path ends.
// Orders by slack, or by negated arrival when the first end is unconstrained,
// because a later arrival is the worse path while a smaller slack is the worse
// path (search/PathEnd.cc:L2091-L2093).  Unlike PathEndLess it applies no pin,
// transition, clock or path tie break at all, so two ends of equal slack are
// simply equivalent under it and their relative order is left undetermined.
// Path grouping sorts a group's endpoints with it so the worst come first, and
// then lets crpr tag de-duplication rather than a tie break decide which of
// them are retained (search/PathGroup.cc:L738, L789).
class PathEndSlackLess
{
public:
  PathEndSlackLess(bool cmp_slack,
                   const StaState *sta);
  bool operator()(const PathEnd *path_end1,
                  const PathEnd *path_end2) const;

protected:
  // Comparator configuration.  Observation: cmp_slack_ is initialized
  // (search/PathEnd.cc:L2082) but operator() never reads it
  // (search/PathEnd.cc:L2088-L2094), so it does not affect this ordering.
  bool cmp_slack_;
  const StaState *sta_;
};

// Orders path ends by identity while ignoring crpr, so that two ends which
// differ only in their crpr tag compare equal.  exceptPathCmp is consulted
// first and its sign is the answer whenever it is nonzero; Path::cmpNoCrpr
// breaks the remaining ties (search/PathEnd.cc:L2104-L2116).  Path grouping
// uses it as the ordering of a std::set (search/PathGroup.cc:L611) so that only
// the worst end per crpr tag is retained, and the code spells out what happens
// to the rest: "Only save the worst path end for each crpr tag." / "PathEnum
// will peel the others." (search/PathGroup.cc:L796-L797).
class PathEndNoCrprLess
{
public:
  PathEndNoCrprLess(const StaState *sta);
  bool operator()(const PathEnd *path_end1,
                  const PathEnd *path_end2) const;

protected:
  // Comparator configuration.  There is no cmp_slack_ here because crpr
  // insensitive identity ordering never consults slack or arrival.
  const StaState *sta_;
};

} // namespace sta
