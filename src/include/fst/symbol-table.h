// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Classes to provide symbol-to-integer and integer-to-symbol mappings.

#ifndef FST_LIB_SYMBOL_TABLE_H_
#define FST_LIB_SYMBOL_TABLE_H_

#include <cstring>
#include <ios>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fst/compat.h>
#include <fstream>
#include <map>

DECLARE_bool(fst_compat_symbols);

namespace fst {

// WARNING: Reading via symbol table read options should
//          not be used. This is a temporary work around for
//          reading symbol ranges of previously stored symbol sets.
struct SymbolTableReadOptions {
  SymbolTableReadOptions() {}

  SymbolTableReadOptions(
      std::vector<std::pair<int64, int64> > string_hash_ranges_,
      const string& source_)
      : string_hash_ranges(std::move(string_hash_ranges_)), source(source_) {}

  std::vector<std::pair<int64, int64> > string_hash_ranges;
  string source;
};

struct SymbolTableTextOptions {
  SymbolTableTextOptions();

  bool allow_negative;
  string fst_field_separator;
};

namespace internal {

// List of symbols with a dense hash for looking up symbol index.
// Hash uses linear probe, rehashes at 0.75% occupancy, avg 6 bytes overhead
// per entry.  Rehash in place from symbol list.
//
// Symbols are stored as c strings to avoid adding memory overhead, but the
// performance penalty for this is high because rehash must call strlen on
// every symbol.  AddSymbol can be another 2x faster if symbol lengths were
// stored.
class DenseSymbolMap {
 public:
  DenseSymbolMap();
  DenseSymbolMap(const DenseSymbolMap& x);
  ~DenseSymbolMap();

  std::pair<int64, bool> InsertOrFind(const string& key);
  int64 Find(const string& key) const;

  const size_t size() const { return symbols_.size(); }
  const string GetSymbol(size_t idx) const {
    return string(symbols_[idx], strlen(symbols_[idx]));
  }
  void RemoveSymbol(size_t idx);

 private:
  // num_buckets must be power of 2.
  void Rehash(size_t num_buckets);
  const char* NewSymbol(const string& sym);

  int64 empty_;
  std::vector<const char*> symbols_;
  std::hash<string> str_hash_;
  std::vector<int64> buckets_;
  uint64 hash_mask_;
  int size_;
};

}  // namespace internal

class SymbolTableImpl {
 public:
  explicit SymbolTableImpl(const string& name)
      : name_(name),
        available_key_(0),
        dense_key_limit_(0),
        check_sum_finalized_(false) {}

  SymbolTableImpl(const SymbolTableImpl& impl)
      : name_(impl.name_),
        available_key_(impl.available_key_),
        dense_key_limit_(impl.dense_key_limit_),
        symbols_(impl.symbols_),
        idx_key_(impl.idx_key_),
        key_map_(impl.key_map_),
        check_sum_finalized_(false) {}

  int64 AddSymbol(const string& symbol, int64 key);

  int64 AddSymbol(const string& symbol) {
    return AddSymbol(symbol, available_key_);
  }

  // Removes the symbol with the given key. The removal is costly
  // (O(NumSymbols)) and may reduce the efficiency of Find() because of a
  // potentially reduced size of the dense key interval.
  void RemoveSymbol(int64 key);

  static SymbolTableImpl* ReadText(
      std::istream& strm, const string& name,  // NOLINT
      const SymbolTableTextOptions& opts = SymbolTableTextOptions());

  static SymbolTableImpl* Read(std::istream& strm,  // NOLINT
                               const SymbolTableReadOptions& opts);

  bool Write(std::ostream& strm) const;  // NOLINT

  // Return the string associated with the key. If the key is out of
  // range (<0, >max), return an empty string.
  string Find(int64 key) const {
    int64 idx = key;
    if (key < 0 || key >= dense_key_limit_) {
      map<int64, int64>::const_iterator iter =
          key_map_.find(key);
      if (iter == key_map_.end()) {
        return "";
      }
      idx = iter->second;
    }
    if (idx < 0 || idx >= symbols_.size()) {
      return "";
    }
    return symbols_.GetSymbol(idx);
  }

  // Return the key associated with the symbol. If the symbol
  // does not exists, return SymbolTable::kNoSymbol.
  int64 Find(const string& symbol) const {
    int64 idx = symbols_.Find(symbol);
    if (idx == -1 || idx < dense_key_limit_) return idx;
    return idx_key_[idx - dense_key_limit_];
  }

  // Return the key associated with the symbol. If the symbol
  // does not exists, return SymbolTable::kNoSymbol.
  int64 Find(const char* symbol) const {
    const string str(symbol);
    return Find(str);
  }

  int64 GetNthKey(ssize_t pos) const {
    if (pos < 0 || pos >= symbols_.size()) return -1;
    if (pos < dense_key_limit_) return pos;
    return Find(symbols_.GetSymbol(pos));
  }

  const string& Name() const { return name_; }

  void SetName(const string &new_name) { name_ = new_name; }

  string CheckSum() const {
    MaybeRecomputeCheckSum();
    return check_sum_string_;
  }

  string LabeledCheckSum() const {
    MaybeRecomputeCheckSum();
    return labeled_check_sum_string_;
  }

  int64 AvailableKey() const { return available_key_; }

  size_t NumSymbols() const { return symbols_.size(); }

 private:
  // Recomputes the checksums (both of them) if we've had changes since the last
  // computation (i.e., if check_sum_finalized_ is false).
  // Takes ~2.5 microseconds (dbg) or ~230 nanoseconds (opt) on a 2.67GHz Xeon
  // if the checksum is up-to-date (requiring no recomputation).
  void MaybeRecomputeCheckSum() const;

  string name_;
  int64 available_key_;
  int64 dense_key_limit_;

  internal::DenseSymbolMap symbols_;
  // Maps index to key for index >= dense_key_limit:
  //   key = idx_key_[index - dense_key_limit]
  std::vector<int64> idx_key_;
  // Maps key to index for key >= dense_key_limit_.
  //  index = key_map_[key]
  map<int64, int64> key_map_;

  mutable bool check_sum_finalized_;
  mutable string check_sum_string_;
  mutable string labeled_check_sum_string_;
  mutable Mutex check_sum_mutex_;
};

//
// \class SymbolTable
// \brief Symbol (string) to int and reverse mapping
//
// The SymbolTable implements the mappings of labels to strings and reverse.
// SymbolTables are used to describe the alphabet of the input and output
// labels for arcs in a Finite State Transducer.
//
// SymbolTables are reference counted and can therefore be shared across
// multiple machines. For example a language model grammar G, with a
// SymbolTable for the words in the language model can share this symbol
// table with the lexical representation L o G.
//
class SymbolTable {
 public:
  static const int64 kNoSymbol = -1;

  // Construct symbol table with an unspecified name.
  SymbolTable() : impl_(std::make_shared<SymbolTableImpl>("<unspecified>")) {}

  // Construct symbol table with a unique name.
  explicit SymbolTable(const string& name)
      : impl_(std::make_shared<SymbolTableImpl>(name)) {}

  virtual ~SymbolTable() {}

  // Read an ascii representation of the symbol table from an istream. Pass a
  // name to give the resulting SymbolTable.
  static SymbolTable* ReadText(
      std::istream& strm, const string& name,  // NOLINT
      const SymbolTableTextOptions& opts = SymbolTableTextOptions()) {
    SymbolTableImpl* impl = SymbolTableImpl::ReadText(strm, name, opts);
    if (!impl) {
      return nullptr;
    } else {
      return new SymbolTable(impl);
    }
  }

  // read an ascii representation of the symbol table
  static SymbolTable* ReadText(
      const string& filename,
      const SymbolTableTextOptions& opts = SymbolTableTextOptions()) {
    std::ifstream strm(filename.c_str(), std::ios_base::in);
    if (!strm.good()) {
      LOG(ERROR) << "SymbolTable::ReadText: Can't open file " << filename;
      return nullptr;
    }
    return ReadText(strm, filename, opts);
  }

  // WARNING: Reading via symbol table read options should
  //          not be used. This is a temporary work around.
  static SymbolTable* Read(std::istream& strm,  // NOLINT
                           const SymbolTableReadOptions& opts) {
    SymbolTableImpl* impl = SymbolTableImpl::Read(strm, opts);
    if (!impl) {
      return nullptr;
    } else {
      return new SymbolTable(impl);
    }
  }

  // read a binary dump of the symbol table from a stream
  static SymbolTable* Read(std::istream& strm,  // NOLINT
                           const string& source) {
    SymbolTableReadOptions opts;
    opts.source = source;
    return Read(strm, opts);
  }

  // read a binary dump of the symbol table
  static SymbolTable* Read(const string& filename) {
    std::ifstream strm(filename.c_str(),
                            std::ios_base::in | std::ios_base::binary);
    if (!strm.good()) {
      LOG(ERROR) << "SymbolTable::Read: Can't open file " << filename;
      return nullptr;
    }
    return Read(strm, filename);
  }

  //--------------------------------------------------------
  // Derivable Interface (final)
  //--------------------------------------------------------
  // create a reference counted copy
  virtual SymbolTable* Copy() const { return new SymbolTable(*this); }

  // Add a symbol with given key to table. A symbol table also
  // keeps track of the last available key (highest key value in
  // the symbol table).
  virtual int64 AddSymbol(const string& symbol, int64 key) {
    MutateCheck();
    return impl_->AddSymbol(symbol, key);
  }

  // Add a symbol to the table. The associated value key is automatically
  // assigned by the symbol table.
  virtual int64 AddSymbol(const string& symbol) {
    MutateCheck();
    return impl_->AddSymbol(symbol);
  }

  // Add another symbol table to this table. All key values will be offset
  // by the current available key (highest key value in the symbol table).
  // Note string symbols with the same key value with still have the same
  // key value after the symbol table has been merged, but a different
  // value. Adding symbol tables do not result in changes in the base table.
  virtual void AddTable(const SymbolTable& table);

  virtual void RemoveSymbol(int64 key) {
    MutateCheck();
    return impl_->RemoveSymbol(key);
  }

  // Returns the name of the symbol table.
  virtual const string& Name() const { return impl_->Name(); }

  // Sets the name of the symbol table.
  virtual void SetName(const string &new_name) {
    MutateCheck();
    impl_->SetName(new_name);
  }

  // Return the label-agnostic MD5 check-sum for this table.  All new symbols
  // added to the table will result in an updated checksum.
  // DEPRECATED.
  virtual string CheckSum() const { return impl_->CheckSum(); }

  // Same as CheckSum(), but this returns an label-dependent version.
  virtual string LabeledCheckSum() const { return impl_->LabeledCheckSum(); }

  virtual bool Write(std::ostream& strm) const {  // NOLINT
    return impl_->Write(strm);
  }

  bool Write(const string& filename) const {
    std::ofstream strm(filename.c_str(),
                             std::ios_base::out | std::ios_base::binary);
    if (!strm.good()) {
      LOG(ERROR) << "SymbolTable::Write: Can't open file " << filename;
      return false;
    }
    return Write(strm);
  }

  // Dump an ascii text representation of the symbol table via a stream
  virtual bool WriteText(
      std::ostream& strm,  // NOLINT
      const SymbolTableTextOptions& opts = SymbolTableTextOptions()) const;

  // Dump an ascii text representation of the symbol table
  bool WriteText(const string& filename) const {
    std::ofstream strm(filename);
    if (!strm.good()) {
      LOG(ERROR) << "SymbolTable::WriteText: Can't open file " << filename;
      return false;
    }
    return WriteText(strm);
  }

  // Return the string associated with the key. If the key is out of
  // range (<0, >max), return an empty string.
  virtual string Find(int64 key) const { return impl_->Find(key); }

  // Return the key associated with the symbol. If the symbol
  // does not exist, return SymbolTable::kNoSymbol
  virtual int64 Find(const string& symbol) const { return impl_->Find(symbol); }

  // Return the key associated with the symbol. If the symbol
  // does not exist, return SymbolTable::kNoSymbol
  virtual int64 Find(const char* symbol) const { return impl_->Find(symbol); }

  // Return the current available key (i.e highest key number+1) in
  // the symbol table
  virtual int64 AvailableKey() const { return impl_->AvailableKey(); }

  // Return the current number of symbols in table (not necessarily
  // equal to AvailableKey())
  virtual size_t NumSymbols() const { return impl_->NumSymbols(); }

  virtual int64 GetNthKey(ssize_t pos) const { return impl_->GetNthKey(pos); }

 private:
  explicit SymbolTable(SymbolTableImpl* impl) : impl_(impl) {}

  void MutateCheck() {
    if (!impl_.unique()) {
      impl_.reset(new SymbolTableImpl(*impl_));
    }
  }

  const SymbolTableImpl* Impl() const { return impl_.get(); }

 private:
  std::shared_ptr<SymbolTableImpl> impl_;
};

//
// \class SymbolTableIterator
// \brief Iterator class for symbols in a symbol table
class SymbolTableIterator {
 public:
  explicit SymbolTableIterator(const SymbolTable& table)
      : table_(table),
        pos_(0),
        nsymbols_(table.NumSymbols()),
        key_(table.GetNthKey(0)) {}

  ~SymbolTableIterator() {}

  // is iterator done
  bool Done() const { return (pos_ == nsymbols_); }

  // return the Value() of the current symbol (int64 key)
  int64 Value() const { return key_; }

  // return the string of the current symbol
  string Symbol() const { return table_.Find(key_); }

  // advance iterator forward
  void Next() {
    ++pos_;
    if (pos_ < nsymbols_) key_ = table_.GetNthKey(pos_);
  }

  // reset iterator
  void Reset() {
    pos_ = 0;
    key_ = table_.GetNthKey(0);
  }

 private:
  const SymbolTable& table_;
  ssize_t pos_;
  size_t nsymbols_;
  int64 key_;
};

// Returns true if the two symbol tables have equal checksums. Passing in
// nullptr for either table always returns true.
inline bool CompatSymbols(const SymbolTable* syms1, const SymbolTable* syms2,
                          bool warning = true) {
  // Flag can explicitly override this check.
  if (!FLAGS_fst_compat_symbols) {
    return true;
  }

  if (syms1 && syms2 &&
      (syms1->LabeledCheckSum() != syms2->LabeledCheckSum())) {
    if (warning) {
      LOG(WARNING) << "CompatSymbols: Symbol table check sums do not match. "
                   << "Table sizes are " << syms1->NumSymbols() << " and "
                   << syms2->NumSymbols();
    }
    return false;
  } else {
    return true;
  }
}

// Relabels a symbol table as specified by the input vector of pairs
// (old label, new label). The new symbol table only retains symbols
// for which a relabeling is *explicitely* specified.
// TODO(allauzen): consider adding options to allow for some form
// of implicit identity relabeling.
template <class Label>
SymbolTable* RelabelSymbolTable(
    const SymbolTable* table,
    const std::vector<std::pair<Label, Label> >& pairs) {
  SymbolTable* new_table =
      new SymbolTable(table->Name().empty() ? string() : (string("relabeled_") +
                                                          table->Name()));

  for (size_t i = 0; i < pairs.size(); ++i) {
    new_table->AddSymbol(table->Find(pairs[i].first), pairs[i].second);
  }

  return new_table;
}

// Symbol Table Serialization
inline void SymbolTableToString(const SymbolTable* table, string* result) {
  std::ostringstream ostrm;
  table->Write(ostrm);
  *result = ostrm.str();
}

inline SymbolTable* StringToSymbolTable(const string& s) {
  std::istringstream istrm(s);
  return SymbolTable::Read(istrm, SymbolTableReadOptions());
}

}  // namespace fst

#endif  // FST_LIB_SYMBOL_TABLE_H_
