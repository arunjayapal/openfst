// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Class to compute the composition of two FSTs.

#ifndef FST_LIB_COMPOSE_H_
#define FST_LIB_COMPOSE_H_

#include <algorithm>
#include <string>
#include <vector>

#include <fst/cache.h>
#include <fst/compose-filter.h>
#include <fst/fst-decl.h>  // For optional argument declarations
#include <fst/lookahead-filter.h>
#include <fst/matcher.h>
#include <fst/state-table.h>
#include <fst/test-properties.h>


namespace fst {

// Delayed composition options templated on the arc type, the matcher,
// the composition filter, and the composition state table.  By
// default, the matchers, filter, and state table are constructed by
// composition. If set below, the user can instead pass in these
// objects; in that case, ComposeFst takes their ownership. This
// version controls composition implemented between generic Fst<Arc>
// types and a shared matcher type M for Fst<Arc>. This should be
// adequate for most applications, giving a reasonable tradeoff
// between efficiency and code sharing (but see ComposeFstImplOptions).
template <class A, class M = Matcher<Fst<A>>,
          class F = SequenceComposeFilter<M>,
          class T = GenericComposeStateTable<A, typename F::FilterState>>
struct ComposeFstOptions : public CacheOptions {
  M *matcher1;     // FST1 matcher (see matcher.h)
  M *matcher2;     // FST2 matcher
  F *filter;       // Composition filter (see compose-filter.h)
  T *state_table;  // Composition state table (see compose-state-table.h)

  explicit ComposeFstOptions(const CacheOptions &opts, M *mat1 = nullptr,
                             M *mat2 = nullptr, F *filt = nullptr,
                             T *sttable = nullptr)
      : CacheOptions(opts),
        matcher1(mat1),
        matcher2(mat2),
        filter(filt),
        state_table(sttable) {}

  ComposeFstOptions()
      : matcher1(nullptr),
        matcher2(nullptr),
        filter(nullptr),
        state_table(nullptr) {}
};

// Delayed composition options templated on the two matcher types, the
// composition filter, the composition state table and the cache
// store.  By default, the matchers, filter, state table and cache
// store are constructed by composition. If set below, the user can
// instead pass in these objects; in that case, ComposeFst takes their
// ownership. This version controls composition implemented using
// arbitrary matchers (of the same Arc type but otherwise arbitrary
// Fst type). The user must ensure the matchers are compatible. These
// options permit the most efficient use, but shares the least
// code. This is for advanced use only in the most demanding or
// specialized applications that can benefit from it (o.w. prefer
// ComposeFstOptions).
template <class M1, class M2, class F = SequenceComposeFilter<M1, M2>,
          class T = GenericComposeStateTable<typename M1::Arc,
                                             typename F::FilterState>,
          class C = DefaultCacheStore<typename M1::Arc>>
struct ComposeFstImplOptions : public CacheImplOptions<C> {
  M1 *matcher1;          // FST1 matcher (see matcher.h)
  M2 *matcher2;          // FST2 matcher
  F *filter;             // Composition filter (see compose-filter.h)
  T *state_table;        // Composition state table (see compose-state-table.h)
  bool own_state_table;  // ComposeFstImpl takes ownership of 'state_table'?

  explicit ComposeFstImplOptions(const CacheOptions &opts, M1 *mat1 = nullptr,
                                 M2 *mat2 = nullptr, F *filt = nullptr,
                                 T *sttable = nullptr)
      : CacheImplOptions<C>(opts),
        matcher1(mat1),
        matcher2(mat2),
        filter(filt),
        state_table(sttable),
        own_state_table(true) {}

  explicit ComposeFstImplOptions(const CacheImplOptions<C> &opts,
                                 M1 *mat1 = nullptr, M2 *mat2 = nullptr,
                                 F *filt = nullptr, T *sttable = nullptr)
      : CacheImplOptions<C>(opts),
        matcher1(mat1),
        matcher2(mat2),
        filter(filt),
        state_table(sttable),
        own_state_table(true) {}

  ComposeFstImplOptions()
      : matcher1(nullptr),
        matcher2(nullptr),
        filter(nullptr),
        state_table(nullptr),
        own_state_table(true) {}
};

// Implementation of delayed composition. This base class is
// common to the variants with different matchers, composition filters
// and state tables.
template <class A, class C = DefaultCacheStore<A>>
class ComposeFstImplBase : public CacheBaseImpl<typename C::State, C> {
 public:
  typedef typename A::Label Label;
  typedef typename A::Weight Weight;
  typedef typename A::StateId StateId;
  typedef typename C::State State;
  typedef CacheBaseImpl<State, C> CImpl;

  using FstImpl<A>::SetType;
  using FstImpl<A>::SetProperties;
  using FstImpl<A>::Properties;
  using FstImpl<A>::SetInputSymbols;
  using FstImpl<A>::SetOutputSymbols;

  using CImpl::HasStart;
  using CImpl::HasFinal;
  using CImpl::HasArcs;
  using CImpl::SetFinal;
  using CImpl::SetStart;

  ComposeFstImplBase(const Fst<A> &fst1, const Fst<A> &fst2,
                     const CacheImplOptions<C> &opts)
      : CImpl(opts) {
    InitBase(fst1, fst2);
  }

  ComposeFstImplBase(const Fst<A> &fst1, const Fst<A> &fst2,
                     const CacheOptions &opts)
      : CImpl(opts) {
    InitBase(fst1, fst2);
  }

  ComposeFstImplBase(const ComposeFstImplBase<A, C> &impl) : CImpl(impl, true) {
    SetType(impl.Type());
    SetProperties(impl.Properties(), kCopyProperties);
    SetInputSymbols(impl.InputSymbols());
    SetOutputSymbols(impl.OutputSymbols());
  }

  virtual ComposeFstImplBase<A, C> *Copy() const = 0;

  ~ComposeFstImplBase() override {}

  StateId Start() {
    if (!HasStart()) {
      StateId start = ComputeStart();
      if (start != kNoStateId) {
        SetStart(start);
      }
    }
    return CImpl::Start();
  }

  Weight Final(StateId s) {
    if (!HasFinal(s)) {
      SetFinal(s, ComputeFinal(s));
    }
    return CImpl::Final(s);
  }

  virtual void Expand(StateId s) = 0;

  size_t NumArcs(StateId s) {
    if (!HasArcs(s)) Expand(s);
    return CImpl::NumArcs(s);
  }

  size_t NumInputEpsilons(StateId s) {
    if (!HasArcs(s)) Expand(s);
    return CImpl::NumInputEpsilons(s);
  }

  size_t NumOutputEpsilons(StateId s) {
    if (!HasArcs(s)) Expand(s);
    return CImpl::NumOutputEpsilons(s);
  }

  void InitArcIterator(StateId s, ArcIteratorData<A> *data) {
    if (!HasArcs(s)) Expand(s);
    CImpl::InitArcIterator(s, data);
  }

  virtual MatcherBase<A> *InitMatcher(const ComposeFst<A, C> &fst,
                                      MatchType match_type) const {
    // Use the default matcher if no override is provided.
    return nullptr;
  }

 protected:
  virtual StateId ComputeStart() = 0;
  virtual Weight ComputeFinal(StateId s) = 0;

  void InitBase(const Fst<A> &fst1, const Fst<A> &fst2) {
    SetType("compose");

    if (!CompatSymbols(fst2.InputSymbols(), fst1.OutputSymbols())) {
      FSTERROR() << "ComposeFst: Output symbol table of 1st argument "
                 << "does not match input symbol table of 2nd argument";
      SetProperties(kError, kError);
    }

    SetInputSymbols(fst1.InputSymbols());
    SetOutputSymbols(fst2.OutputSymbols());
  }
};

// Forward declaration of ComposeFstMatcher
template <class C, class F, class T>
class ComposeFstMatcher;

// Implementaion of delayed composition templated on the matchers (see
// matcher.h), composition filter (see compose-filter.h) and
// the composition state table (see compose-state-table.h).
template <class C, class F, class T>
class ComposeFstImpl : public ComposeFstImplBase<typename C::Arc, C> {
  typedef typename F::Matcher1 M1;
  typedef typename F::Matcher2 M2;
  friend class ComposeFstMatcher<C, F, T>;

  typedef typename M1::FST FST1;
  typedef typename M2::FST FST2;
  typedef typename M1::Arc Arc;
  typedef typename Arc::StateId StateId;
  typedef typename Arc::Label Label;
  typedef typename Arc::Weight Weight;
  typedef typename F::FilterState FilterState;
  typedef typename C::State State;
  typedef CacheBaseImpl<State, C> CImpl;
  typedef typename T::StateTuple StateTuple;

  using FstImpl<Arc>::SetType;
  using FstImpl<Arc>::SetProperties;

 public:
  template <class Mat1, class Mat2>
  ComposeFstImpl(const FST1 &fst1, const FST2 &fst2,
                 const ComposeFstImplOptions<Mat1, Mat2, F, T, C> &opts);

  ComposeFstImpl(const ComposeFstImpl<C, F, T> &impl)
      : ComposeFstImplBase<Arc, C>(impl),
        filter_(new F(*impl.filter_, true)),
        matcher1_(filter_->GetMatcher1()),
        matcher2_(filter_->GetMatcher2()),
        fst1_(matcher1_->GetFst()),
        fst2_(matcher2_->GetFst()),
        state_table_(new T(*impl.state_table_)),
        own_state_table_(true),
        match_type_(impl.match_type_) {}

  ~ComposeFstImpl() override {
    if (own_state_table_) delete state_table_;
  }

  ComposeFstImpl<C, F, T> *Copy() const override {
    return new ComposeFstImpl<C, F, T>(*this);
  }

  uint64 Properties() const override { return Properties(kFstProperties); }

  // Set error if found; return FST impl properties.
  uint64 Properties(uint64 mask) const override {
    if ((mask & kError) &&
        (fst1_.Properties(kError, false) || fst2_.Properties(kError, false) ||
         (matcher1_->Properties(0) & kError) ||
         (matcher2_->Properties(0) & kError) |
             (filter_->Properties(0) & kError) ||
         state_table_->Error())) {
      SetProperties(kError, kError);
    }
    return FstImpl<Arc>::Properties(mask);
  }

  // Arranges it so that the first arg to OrderedExpand is the Fst
  // that will be matched on.
  void Expand(StateId s) override {
    const StateTuple &tuple = state_table_->Tuple(s);
    const StateId s1 = tuple.StateId1();
    const StateId s2 = tuple.StateId2();
    filter_->SetState(s1, s2, tuple.GetFilterState());
    if (MatchInput(s1, s2)) {
      OrderedExpand(s, fst2_, s2, fst1_, s1, matcher2_, true);
    } else {
      OrderedExpand(s, fst1_, s1, fst2_, s2, matcher1_, false);
    }
  }

  const FST1 &GetFst1() const { return fst1_; }
  const FST2 &GetFst2() const { return fst2_; }

  const M1 *GetMatcher1() const { return matcher1_; }
  M1 *GetMatcher1() { return matcher1_; }

  const M2 *GetMatcher2() const { return matcher2_; }
  M2 *GetMatcher2() { return matcher2_; }

  const F *GetFilter() const { return filter_.get(); }
  F *GetFilter() { return filter_.get(); }

  const T *GetStateTable() const { return state_table_; }
  T *GetStateTable() { return state_table_; }

  MatcherBase<Arc> *InitMatcher(const ComposeFst<Arc, C> &fst,
                                MatchType match_type) const override {
    uint64 test_props = match_type == MATCH_INPUT
                            ? kFstProperties & ~kILabelInvariantProperties
                            : kFstProperties & ~kOLabelInvariantProperties;
    // If both matchers support 'match_type' and we have
    // a guarantee that a call to 'filter_->FilterArc(arc1, arc2)' will
    // not modify the ilabel of arc1 when MATCH_INPUT or the olabel
    // or arc2 when MATCH_OUTPUT, then ComposeFstMatcher can be used.
    if ((matcher1_->Type(false) == match_type) &&
        (matcher2_->Type(false) == match_type) &&
        (filter_->Properties(test_props) == test_props)) {
      return new ComposeFstMatcher<C, F, T>(fst, this, match_type);
    }
    return nullptr;
  }

 private:
  // This does that actual matching of labels in the composition. The
  // arguments are ordered so matching is called on state 'sa' of
  // 'fsta' for each arc leaving state 'sb' of 'fstb'. The 'match_input' arg
  // determines whether the input or output label of arcs at 'sb' is
  // the one to match on.
  template <class FST, class Matcher>
  void OrderedExpand(StateId s, const Fst<Arc> &, StateId sa, const FST &fstb,
                     StateId sb, Matcher *matchera, bool match_input) {
    matchera->SetState(sa);

    // First process non-consuming symbols (e.g., epsilons) on FSTA.
    Arc loop(match_input ? 0 : kNoLabel, match_input ? kNoLabel : 0,
             Weight::One(), sb);
    MatchArc(s, matchera, loop, match_input);

    // Then process matches on FSTB.
    for (ArcIterator<FST> iterb(fstb, sb); !iterb.Done(); iterb.Next()) {
      MatchArc(s, matchera, iterb.Value(), match_input);
    }

    CImpl::SetArcs(s);
  }

  // Matches a single transition from 'fstb' against 'fata' at 's'.
  template <class Matcher>
  void MatchArc(StateId s, Matcher *matchera, const Arc &arc,
                bool match_input) {
    if (matchera->Find(match_input ? arc.olabel : arc.ilabel)) {
      for (; !matchera->Done(); matchera->Next()) {
        Arc arca = matchera->Value();
        Arc arcb = arc;
        if (match_input) {
          const FilterState &f = filter_->FilterArc(&arcb, &arca);
          if (f != FilterState::NoState()) AddArc(s, arcb, arca, f);
        } else {
          const FilterState &f = filter_->FilterArc(&arca, &arcb);
          if (f != FilterState::NoState()) AddArc(s, arca, arcb, f);
        }
      }
    }
  }

  // Add a matching transition at 's'.
  void AddArc(StateId s, const Arc &arc1, const Arc &arc2,
              const FilterState &f) {
    StateTuple tuple(arc1.nextstate, arc2.nextstate, f);
    Arc oarc(arc1.ilabel, arc2.olabel, Times(arc1.weight, arc2.weight),
             state_table_->FindState(tuple));
    CImpl::PushArc(s, oarc);
  }

  StateId ComputeStart() override {
    StateId s1 = fst1_.Start();
    if (s1 == kNoStateId) return kNoStateId;

    StateId s2 = fst2_.Start();
    if (s2 == kNoStateId) return kNoStateId;

    const FilterState &f = filter_->Start();
    StateTuple tuple(s1, s2, f);
    return state_table_->FindState(tuple);
  }

  Weight ComputeFinal(StateId s) override {
    const StateTuple &tuple = state_table_->Tuple(s);
    const StateId s1 = tuple.StateId1();
    Weight final1 = matcher1_->Final(s1);
    if (final1 == Weight::Zero()) return final1;

    const StateId s2 = tuple.StateId2();
    Weight final2 = matcher2_->Final(s2);
    if (final2 == Weight::Zero()) return final2;

    filter_->SetState(s1, s2, tuple.GetFilterState());
    filter_->FilterFinal(&final1, &final2);
    return Times(final1, final2);
  }

  // Determines which side to match on per composition state.
  bool MatchInput(StateId s1, StateId s2) {
    switch (match_type_) {
      case MATCH_INPUT:
        return true;
      case MATCH_OUTPUT:
        return false;
      default:  // MATCH_BOTH
        ssize_t priority1 = matcher1_->Priority(s1);
        ssize_t priority2 = matcher2_->Priority(s2);

        if (priority1 == kRequirePriority && priority2 == kRequirePriority) {
          FSTERROR() << "ComposeFst: Both sides can't require match";
          SetProperties(kError, kError);
          return true;
        }

        if (priority1 == kRequirePriority) return false;
        if (priority2 == kRequirePriority) {
          return true;
        }
        return priority1 <= priority2;
    }
  }

  // Identifies and verifies the capabilities of the matcher to be used for
  // composition.
  void SetMatchType();

  std::unique_ptr<F> filter_;
  M1 *matcher1_;  // Borrowed reference.
  M2 *matcher2_;  // Borrowed reference.
  const FST1 &fst1_;
  const FST2 &fst2_;
  T *state_table_;
  bool own_state_table_;

  MatchType match_type_;
};

template <class C, class F, class T>
template <class Mat1, class Mat2>
ComposeFstImpl<C, F, T>::ComposeFstImpl(
    const FST1 &fst1, const FST2 &fst2,
    const ComposeFstImplOptions<Mat1, Mat2, F, T, C> &opts)
    : ComposeFstImplBase<Arc, C>(fst1, fst2, opts),
      filter_(opts.filter ? opts.filter
                          : new F(fst1, fst2, opts.matcher1, opts.matcher2)),
      matcher1_(filter_->GetMatcher1()),
      matcher2_(filter_->GetMatcher2()),
      fst1_(matcher1_->GetFst()),
      fst2_(matcher2_->GetFst()),
      state_table_(opts.state_table ? opts.state_table : new T(fst1_, fst2_)),
      own_state_table_(opts.state_table ? opts.own_state_table : true) {
  SetMatchType();
  if (match_type_ == MATCH_NONE) SetProperties(kError, kError);

  uint64 fprops1 = fst1.Properties(kFstProperties, false);
  uint64 fprops2 = fst2.Properties(kFstProperties, false);
  uint64 mprops1 = matcher1_->Properties(fprops1);
  uint64 mprops2 = matcher2_->Properties(fprops2);
  uint64 cprops = ComposeProperties(mprops1, mprops2);
  SetProperties(filter_->Properties(cprops), kCopyProperties);
  if (state_table_->Error()) SetProperties(kError, kError);
}

template <class C, class F, class T>
void ComposeFstImpl<C, F, T>::SetMatchType() {
  // Ensures any required matching is possible and known.
  if ((matcher1_->Flags() & kRequireMatch) &&
      matcher1_->Type(true) != MATCH_OUTPUT) {
    FSTERROR() << "ComposeFst: 1st argument cannot perform required matching.";
    match_type_ = MATCH_NONE;
    return;
  }
  if ((matcher2_->Flags() & kRequireMatch) &&
      matcher2_->Type(true) != MATCH_INPUT) {
    FSTERROR() << "ComposeFst: 2nd argument cannot perform required matching.";
    match_type_ = MATCH_NONE;
    return;
  }

  // Finds which sides to match on (favoring minimal testing of capabilities).
  MatchType type1 = matcher1_->Type(false);
  MatchType type2 = matcher2_->Type(false);
  if (type1 == MATCH_OUTPUT && type2 == MATCH_INPUT) {
    match_type_ = MATCH_BOTH;
  } else if (type1 == MATCH_OUTPUT) {
    match_type_ = MATCH_OUTPUT;
  } else if (type2 == MATCH_INPUT) {
    match_type_ = MATCH_INPUT;
  } else if (matcher1_->Type(true) == MATCH_OUTPUT) {
    match_type_ = MATCH_OUTPUT;
  } else if (matcher2_->Type(true) == MATCH_INPUT) {
    match_type_ = MATCH_INPUT;
  } else {
    FSTERROR() << "ComposeFst: 1st argument cannot match on output labels "
               << "and 2nd argument cannot match on input labels (sort?).";
    match_type_ = MATCH_NONE;
  }
}

// Computes the composition of two transducers. This version is a
// delayed Fst. If FST1 transduces string x to y with weight a and FST2
// transduces y to z with weight b, then their composition transduces
// string x to z with weight Times(x, z).
//
// The output labels of the first transducer or the input labels of
// the second transducer must be sorted (with the default matcher).
// The weights need to form a commutative semiring (valid for
// TropicalWeight and LogWeight).
//
// Complexity:
// Assuming the first FST is unsorted and the second is sorted:
// - Time: O(v1 v2 d1 (log d2 + m2)),
// - Space: O(v1 v2)
// where vi = # of states visited, di = maximum out-degree, and mi the
// maximum multiplicity of the states visited for the ith
// FST. Constant time and space to visit an input state or arc is
// assumed and exclusive of caching.
//
// Caveats:
// - ComposeFst does not trim its output (since it is a delayed operation).
// - The efficiency of composition can be strongly affected by several factors:
//   - the choice of which transducer is sorted - prefer sorting the FST
//     that has the greater average out-degree.
//   - the amount of non-determinism
//   - the presence and location of epsilon transitions - avoid epsilon
//     transitions on the output side of the first transducer or
//     the input side of the second transducer or prefer placing
//     them later in a path since they delay matching and can
//     introduce non-coaccessible states and transitions.
//
// This class attaches interface to implementation and handles
// reference counting, delegating most methods to ImplToFst.
// The type C specifies the cache store (default declared in fst-decl.h).
template <class A, class C /* = DefaultCacheStore<A> */>
class ComposeFst : public ImplToFst<ComposeFstImplBase<A, C>> {
 public:
  friend class ArcIterator<ComposeFst<A, C>>;
  friend class StateIterator<ComposeFst<A, C>>;

  typedef A Arc;
  typedef C Store;
  typedef typename A::Weight Weight;
  typedef typename A::StateId StateId;
  typedef typename C::State State;

  typedef ComposeFstImplBase<A, C> Impl;

  // Compose specifying only caching options.
  ComposeFst(const Fst<A> &fst1, const Fst<A> &fst2,
             const CacheOptions &opts = CacheOptions())
      : ImplToFst<Impl>(CreateBase(fst1, fst2, opts)) {}

  // Compose specifying one shared matcher type M.  Requires input
  // Fsts and matcher FST type (M::FST) be Fst<A>. Recommended for
  // best code-sharing and matcher compatiblity.
  template <class M, class F, class T>
  ComposeFst(const Fst<A> &fst1, const Fst<A> &fst2,
             const ComposeFstOptions<A, M, F, T> &opts)
      : ImplToFst<Impl>(CreateBase1(fst1, fst2, opts)) {}

  // Compose specifying two matcher types M1 and M2.  Requires input
  // Fsts (of the same Arc type but o.w. arbitrary) match the
  // corresponding matcher FST types (M1::FST, M2::FST). Recommended
  // only for advanced use in demanding or specialized applications
  // due to potential code bloat and matcher incompatibilities.
  template <class M1, class M2, class F, class T>
  ComposeFst(const typename M1::FST &fst1, const typename M2::FST &fst2,
             const ComposeFstImplOptions<M1, M2, F, T, C> &opts)
      : ImplToFst<Impl>(CreateBase2(fst1, fst2, opts)) {}

  // See Fst<>::Copy() for doc.
  ComposeFst(const ComposeFst<A, C> &fst, bool safe = false)
      : ImplToFst<Impl>(safe ? std::shared_ptr<Impl>(fst.GetImpl()->Copy())
                             : fst.GetSharedImpl()) {}

  // Get a copy of this ComposeFst. See Fst<>::Copy() for further doc.
  ComposeFst<A, C> *Copy(bool safe = false) const override {
    return new ComposeFst<A, C>(*this, safe);
  }

  inline void InitStateIterator(StateIteratorData<A> *data) const override;

  void InitArcIterator(StateId s, ArcIteratorData<A> *data) const override {
    GetMutableImpl()->InitArcIterator(s, data);
  }

  MatcherBase<A> *InitMatcher(MatchType match_type) const override {
    return GetImpl()->InitMatcher(*this, match_type);
  }

 protected:
  using ImplToFst<Impl>::GetImpl;
  using ImplToFst<Impl>::GetMutableImpl;

  explicit ComposeFst(std::shared_ptr<Impl> impl) : ImplToFst<Impl>(impl) {}

  // Create compose implementation specifying two matcher types.
  template <class M1, class M2, class F, class T>
  static std::shared_ptr<Impl> CreateBase2(
      const typename M1::FST &fst1, const typename M2::FST &fst2,
      const ComposeFstImplOptions<M1, M2, F, T, C> &opts) {
    std::shared_ptr<Impl> impl =
        std::make_shared<ComposeFstImpl<C, F, T>>(fst1, fst2, opts);
    if (!(Weight::Properties() & kCommutative)) {
      int64 props1 = fst1.Properties(kUnweighted, true);
      int64 props2 = fst2.Properties(kUnweighted, true);
      if (!(props1 & kUnweighted) && !(props2 & kUnweighted)) {
        FSTERROR() << "ComposeFst: Weights must be a commutative semiring: "
                   << Weight::Type();
        impl->SetProperties(kError, kError);
      }
    }
    return impl;
  }

  // Create compose implementation specifying one matcher type.
  //  Requires input Fsts and matcher FST type (M::FST) be Fst<A>
  template <class M, class F, class T>
  static std::shared_ptr<Impl> CreateBase1(
      const Fst<A> &fst1, const Fst<A> &fst2,
      const ComposeFstOptions<A, M, F, T> &opts) {
    ComposeFstImplOptions<M, M, F, T, C> nopts(
        opts, opts.matcher1, opts.matcher2, opts.filter, opts.state_table);
    return CreateBase2(fst1, fst2, nopts);
  }

  // Create compose implementation specifying no matcher type.
  static std::shared_ptr<Impl> CreateBase(const Fst<A> &fst1,
                                          const Fst<A> &fst2,
                                          const CacheOptions &opts) {
    switch (LookAheadMatchType(fst1, fst2)) {  // Check for lookahead matchers
      default:
      case MATCH_NONE: {  // Default composition (no look-ahead)
        ComposeFstOptions<Arc> nopts(opts);
        return CreateBase1(fst1, fst2, nopts);
      }
      case MATCH_OUTPUT: {  // Lookahead on fst1
        typedef typename DefaultLookAhead<Arc, MATCH_OUTPUT>::FstMatcher M;
        typedef typename DefaultLookAhead<Arc, MATCH_OUTPUT>::ComposeFilter F;
        ComposeFstOptions<Arc, M, F> nopts(opts);
        return CreateBase1(fst1, fst2, nopts);
      }
      case MATCH_INPUT: {  // Lookahead on fst2
        typedef typename DefaultLookAhead<Arc, MATCH_INPUT>::FstMatcher M;
        typedef typename DefaultLookAhead<Arc, MATCH_INPUT>::ComposeFilter F;
        ComposeFstOptions<Arc, M, F> nopts(opts);
        return CreateBase1(fst1, fst2, nopts);
      }
    }
  }

 private:
  ComposeFst &operator=(const ComposeFst &fst) = delete;
};

// Specialization for ComposeFst.
template <class A, class C>
class StateIterator<ComposeFst<A, C>>
    : public CacheStateIterator<ComposeFst<A, C>> {
 public:
  explicit StateIterator(const ComposeFst<A, C> &fst)
      : CacheStateIterator<ComposeFst<A, C>>(fst, fst.GetMutableImpl()) {}
};

// Specialization for ComposeFst.
template <class A, class C>
class ArcIterator<ComposeFst<A, C>>
    : public CacheArcIterator<ComposeFst<A, C>> {
 public:
  typedef typename A::StateId StateId;

  ArcIterator(const ComposeFst<A, C> &fst, StateId s)
      : CacheArcIterator<ComposeFst<A, C>>(fst.GetMutableImpl(), s) {
    if (!fst.GetImpl()->HasArcs(s)) fst.GetMutableImpl()->Expand(s);
  }
};

template <class A, class C>
inline void ComposeFst<A, C>::InitStateIterator(
    StateIteratorData<A> *data) const {
  data->base = new StateIterator<ComposeFst<A, C>>(*this);
}

// Specialized matcher for ComposeFst.
// Supports MATCH_INPUT (resp. MATCH_OUTPUT) iff the underlying
// matchers for the two Fsts being composed support
// MATCH_INPUT (resp. MATCH_OUTPUT)
template <class C, class F, class T>
class ComposeFstMatcher : public MatcherBase<typename C::Arc> {
 public:
  typedef typename C::Arc Arc;
  typedef typename Arc::Label Label;
  typedef typename Arc::StateId StateId;
  typedef typename F::FilterState FilterState;
  typedef typename F::Matcher1 Matcher1;
  typedef typename F::Matcher2 Matcher2;
  typedef typename T::StateTuple StateTuple;

  ComposeFstMatcher(const ComposeFst<Arc, C> &fst,
                    const ComposeFstImpl<C, F, T> *impl, MatchType match_type)
      : fst_(fst),
        impl_(impl),
        s_(kNoStateId),
        match_type_(match_type),
        matcher1_(impl->matcher1_->Copy()),
        matcher2_(impl->matcher2_->Copy()),
        current_loop_(false),
        loop_(kNoLabel, 0, Arc::Weight::One(), kNoStateId),
        error_(false) {
    if (match_type == MATCH_OUTPUT) std::swap(loop_.ilabel, loop_.olabel);
  }

  ComposeFstMatcher(const ComposeFstMatcher<C, F, T> &matcher,
                    bool safe = false)
      : fst_(matcher.fst_),
        impl_(matcher.impl_),
        s_(fst::kNoStateId),
        match_type_(matcher.match_type_),
        matcher1_(matcher.matcher1_->Copy(safe)),
        matcher2_(matcher.matcher2_->Copy(safe)),
        current_loop_(false),
        loop_(fst::kNoLabel, 0, Arc::Weight::One(), fst::kNoStateId),
        error_(matcher.error_) {
    if (safe == true) {
      FSTERROR() << "ComposeFstMatcher: Safe copy not supported";
      error_ = true;
    }
    if (match_type_ == MATCH_OUTPUT) std::swap(loop_.ilabel, loop_.olabel);
  }

  ComposeFstMatcher<C, F, T> *Copy(bool safe = false) const override {
    return new ComposeFstMatcher<C, F, T>(*this, safe);
  }

  MatchType Type(bool test) const override {
    if ((matcher1_->Type(test) == MATCH_NONE) ||
        (matcher2_->Type(test) == MATCH_NONE)) {
      return MATCH_NONE;
    }
    if (((matcher1_->Type(test) == MATCH_UNKNOWN) &&
         (matcher2_->Type(test) == MATCH_UNKNOWN)) ||
        ((matcher1_->Type(test) == MATCH_UNKNOWN) &&
         (matcher2_->Type(test) == match_type_)) ||
        ((matcher1_->Type(test) == match_type_) &&
         (matcher2_->Type(test) == MATCH_UNKNOWN))) {
      return MATCH_UNKNOWN;
    }
    if ((matcher1_->Type(test) == match_type_) &&
        (matcher2_->Type(test) == match_type_)) {
      return match_type_;
    }
    return MATCH_NONE;
  }

  const Fst<Arc> &GetFst() const override { return fst_; }

  uint64 Properties(uint64 inprops) const override {
    uint64 outprops = inprops;
    if (error_) outprops |= kError;
    return outprops;
  }

  // Processes a match with the filter and creates resulting arc.
  bool MatchArc(StateId s, Arc arc1, Arc arc2) {
    const FilterState &f = impl_->filter_->FilterArc(&arc1, &arc2);
    if (f == FilterState::NoState()) return false;
    StateTuple tuple(arc1.nextstate, arc2.nextstate, f);
    arc_.ilabel = arc1.ilabel;
    arc_.olabel = arc2.olabel;
    arc_.weight = Times(arc1.weight, arc2.weight);
    arc_.nextstate = impl_->state_table_->FindState(tuple);
    return true;
  }

  // Finds the first match allowed by the filter.
  template <class MatcherA, class MatcherB>
  bool FindLabel(Label label, MatcherA *matchera, MatcherB *matcherb) {
    if (matchera->Find(label)) {
      matcherb->Find(match_type_ == MATCH_INPUT ? matchera->Value().olabel
                                                : matchera->Value().ilabel);
      return FindNext(matchera, matcherb);
    }
    return false;
  }

  // Finds the next match allowed by the filter:
  // Returns true if such a match is found.
  template <class MatcherA, class MatcherB>
  bool FindNext(MatcherA *matchera, MatcherB *matcherb) {
    // State when entering this function:
    // 'matchera' is pointed to a match (x,y) for label x, and a match for y was
    // requested on 'matcherb'.
    while (!matchera->Done() || !matcherb->Done()) {
      if (matcherb->Done()) {
        // If no more matches for y on 'matcherb'
        // move forward on 'matchera' until a match (x,y') is found
        // such that there is a match for y' on 'matcherb'.
        matchera->Next();
        while (!matchera->Done() &&
               !matcherb->Find(match_type_ == MATCH_INPUT
                                   ? matchera->Value().olabel
                                   : matchera->Value().ilabel)) {
          matchera->Next();
        }
      }
      while (!matcherb->Done()) {
        // 'matchera' is pointing to a match (x,y') ('arca') and
        // 'matcherb' is pointing to a match (y',z') ('arcb').
        // If combining these two arcs is allowed by the filter
        // (hence resulting in an arc (x,z')) return true.
        // Position 'matcherb' on the next potential match for y' before
        // returning.
        const Arc &arca = matchera->Value();
        const Arc &arcb = matcherb->Value();
        // Position 'matcherb' on the next potential match for y'.
        matcherb->Next();
        // If combining these two arcs is allowed by the filter
        // (hence resulting in an arc (x,z')) return true.
        // returning. Otherwise consider next match for y' on 'matcherb'.
        if (MatchArc(s_, match_type_ == MATCH_INPUT ? arca : arcb,
                     match_type_ == MATCH_INPUT ? arcb : arca)) {
          return true;
        }
      }
    }
    // Both 'matchera' and 'matcherb' are done, no more match to analyse.
    return false;
  }

 private:
  void SetState_(StateId s) override {
    if (s_ == s) return;
    s_ = s;
    StateTuple tuple = impl_->state_table_->Tuple(s);
    matcher1_->SetState(tuple.StateId1());
    matcher2_->SetState(tuple.StateId2());
    loop_.nextstate = s_;
  }

  bool Find_(Label label) override {
    bool found = false;
    current_loop_ = false;
    if (label == 0) {
      current_loop_ = true;
      found = true;
    }
    if (match_type_ == MATCH_INPUT) {
      found = found || FindLabel(label, matcher1_.get(), matcher2_.get());
    } else {  // match_type_ == MATCH_OUTPUT
      found = found || FindLabel(label, matcher2_.get(), matcher1_.get());
    }
    return found;
  }

  bool Done_() const override {
    return !current_loop_ && matcher1_->Done() && matcher2_->Done();
  }

  const Arc &Value_() const override { return current_loop_ ? loop_ : arc_; }

  void Next_() override {
    if (current_loop_) {
      current_loop_ = false;
    } else if (match_type_ == MATCH_INPUT) {
      FindNext(matcher1_.get(), matcher2_.get());
    } else {  // match_type_ == MATCH_OUTPUT
      FindNext(matcher2_.get(), matcher1_.get());
    }
  }

  ssize_t Priority_(StateId s) override { return fst_.NumArcs(s); }

 private:
  const ComposeFst<Arc, C> &fst_;
  const ComposeFstImpl<C, F, T> *impl_;
  StateId s_;
  MatchType match_type_;
  std::unique_ptr<Matcher1> matcher1_;
  std::unique_ptr<Matcher2> matcher2_;
  bool current_loop_;
  Arc loop_;
  Arc arc_;
  bool error_;
};

// Useful alias when using StdArc.
typedef ComposeFst<StdArc> StdComposeFst;

enum ComposeFilter {
  AUTO_FILTER,
  NULL_FILTER,
  TRIVIAL_FILTER,
  SEQUENCE_FILTER,
  ALT_SEQUENCE_FILTER,
  MATCH_FILTER
};

struct ComposeOptions {
  bool connect;               // Connect output
  ComposeFilter filter_type;  // Which pre-defined filter to use

  explicit ComposeOptions(bool c, ComposeFilter ft = AUTO_FILTER)
      : connect(c), filter_type(ft) {}
  ComposeOptions() : connect(true), filter_type(AUTO_FILTER) {}
};

// Computes the composition of two transducers. This version writes
// the composed FST into a MutableFst. If FST1 transduces string x to
// y with weight a and FST2 transduces y to z with weight b, then
// their composition transduces string x to z with weight
// Times(x, z).
//
// The output labels of the first transducer or the input labels of
// the second transducer must be sorted.  The weights need to form a
// commutative semiring (valid for TropicalWeight and LogWeight).
//
// Complexity:
// Assuming the first FST is unsorted and the second is sorted:
// - Time: O(V1 V2 D1 (log D2 + M2)),
// - Space: O(V1 V2 D1 M2)
// where Vi = # of states, Di = maximum out-degree, and Mi is
// the maximum multiplicity for the ith FST.
//
// Caveats:
// - Compose trims its output.
// - The efficiency of composition can be strongly affected by several factors:
//   - the choice of which transducer is sorted - prefer sorting the FST
//     that has the greater average out-degree.
//   - the amount of non-determinism
//   - the presence and location of epsilon transitions - avoid epsilon
//     transitions on the output side of the first transducer or
//     the input side of the second transducer or prefer placing
//     them later in a path since they delay matching and can
//     introduce non-coaccessible states and transitions.
template <class Arc>
void Compose(const Fst<Arc> &ifst1, const Fst<Arc> &ifst2,
             MutableFst<Arc> *ofst,
             const ComposeOptions &opts = ComposeOptions()) {
  typedef Matcher<Fst<Arc>> M;

  if (opts.filter_type == AUTO_FILTER) {
    CacheOptions nopts;
    nopts.gc_limit = 0;  // Cache only the last state for fastest copy.
    *ofst = ComposeFst<Arc>(ifst1, ifst2, nopts);
  } else if (opts.filter_type == NULL_FILTER) {
    ComposeFstOptions<Arc, M, NullComposeFilter<M>> copts;
    copts.gc_limit = 0;  // Cache only the last state for fastest copy.
    *ofst = ComposeFst<Arc>(ifst1, ifst2, copts);
  } else if (opts.filter_type == SEQUENCE_FILTER) {
    ComposeFstOptions<Arc, M, SequenceComposeFilter<M>> copts;
    copts.gc_limit = 0;  // Cache only the last state for fastest copy.
    *ofst = ComposeFst<Arc>(ifst1, ifst2, copts);
  } else if (opts.filter_type == ALT_SEQUENCE_FILTER) {
    ComposeFstOptions<Arc, M, AltSequenceComposeFilter<M>> copts;
    copts.gc_limit = 0;  // Cache only the last state for fastest copy.
    *ofst = ComposeFst<Arc>(ifst1, ifst2, copts);
  } else if (opts.filter_type == MATCH_FILTER) {
    ComposeFstOptions<Arc, M, MatchComposeFilter<M>> copts;
    copts.gc_limit = 0;  // Cache only the last state for fastest copy.
    *ofst = ComposeFst<Arc>(ifst1, ifst2, copts);
  } else if (opts.filter_type == TRIVIAL_FILTER) {
    ComposeFstOptions<Arc, M, TrivialComposeFilter<M>> copts;
    copts.gc_limit = 0;  // Cache only the last state for fastest copy.
    *ofst = ComposeFst<Arc>(ifst1, ifst2, copts);
  }

  if (opts.connect) Connect(ofst);
}

}  // namespace fst

#endif  // FST_LIB_COMPOSE_H_
