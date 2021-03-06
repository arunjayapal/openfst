// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Topological sort of FSTs.

#ifndef FST_LIB_TOPSORT_H_
#define FST_LIB_TOPSORT_H_

#include <algorithm>
#include <vector>


#include <fst/dfs-visit.h>
#include <fst/fst.h>
#include <fst/statesort.h>


namespace fst {

// DFS visitor class to return topological ordering.
template <class A>
class TopOrderVisitor {
 public:
  typedef A Arc;
  typedef typename A::StateId StateId;

  // If acyclic, ORDER[i] gives the topological position of state Id i;
  // otherwise unchanged. ACYCLIC will be true iff the FST has
  // no cycles.
  TopOrderVisitor(std::vector<StateId> *order, bool *acyclic)
      : order_(order), acyclic_(acyclic) {}

  void InitVisit(const Fst<A> &fst) {
    finish_.reset(new std::vector<StateId>());
    *acyclic_ = true;
  }

  bool InitState(StateId s, StateId r) { return true; }

  bool TreeArc(StateId s, const A &arc) { return true; }

  bool BackArc(StateId s, const A &arc) { return (*acyclic_ = false); }

  bool ForwardOrCrossArc(StateId s, const A &arc) { return true; }

  void FinishState(StateId s, StateId p, const A *) { finish_->push_back(s); }

  void FinishVisit() {
    if (*acyclic_) {
      order_->clear();
      for (StateId s = 0; s < finish_->size(); ++s) {
        order_->push_back(kNoStateId);
      }
      for (StateId s = 0; s < finish_->size(); ++s) {
        (*order_)[(*finish_)[finish_->size() - s - 1]] = s;
      }
    }
    finish_.reset();
  }

 private:
  std::vector<StateId> *order_;
  bool *acyclic_;
  std::unique_ptr<std::vector<StateId>>
      finish_;  // states in finishing-time order
};

// Topologically sorts its input if acyclic, modifying it.  Otherwise,
// the input is unchanged.  When sorted, all transitions are from
// lower to higher state IDs.
//
// Complexity:
// - Time:  O(V + E)
// - Space: O(V + E)
// where V = # of states and E = # of arcs.
template <class Arc>
bool TopSort(MutableFst<Arc> *fst) {
  typedef typename Arc::StateId StateId;

  std::vector<StateId> order;
  bool acyclic;

  TopOrderVisitor<Arc> top_order_visitor(&order, &acyclic);
  DfsVisit(*fst, &top_order_visitor);

  if (acyclic) {
    StateSort(fst, order);
    fst->SetProperties(kAcyclic | kInitialAcyclic | kTopSorted,
                       kAcyclic | kInitialAcyclic | kTopSorted);
  } else {
    fst->SetProperties(kCyclic | kNotTopSorted, kCyclic | kNotTopSorted);
  }
  return acyclic;
}

}  // namespace fst

#endif  // FST_LIB_TOPSORT_H_
