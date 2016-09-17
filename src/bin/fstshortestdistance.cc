// See www.openfst.org for extensive documentation on this weighted
// finite-state transducer library.
//
// Find shortest distances in an FST.

#include <memory>
#include <string>
#include <vector>

#include <fst/script/getters.h>
#include <fst/script/shortest-distance.h>
#include <fst/script/text-io.h>

DEFINE_bool(reverse, false, "Perform in the reverse direction");
DEFINE_double(delta, fst::kDelta, "Comparison/quantization delta");
DEFINE_int64(nstate, fst::kNoStateId, "State number threshold");
DEFINE_string(queue_type, "auto",
              "Queue type: one of: \"auto\", "
              "\"fifo\", \"lifo\", \"shortest\", \"state\", \"top\"");

int main(int argc, char **argv) {
  namespace s = fst::script;
  using fst::script::FstClass;
  using fst::script::WeightClass;

  string usage = "Finds shortest distance(s) in an FST.\n\n  Usage: ";
  usage += argv[0];
  usage += " [in.fst [distance.txt]]\n";

  std::set_new_handler(FailedNewHandler);
  SET_FLAGS(usage.c_str(), &argc, &argv, true);
  if (argc > 3) {
    ShowUsage();
    return 1;
  }

  string in_fname = (argc > 1 && (strcmp(argv[1], "-") != 0)) ? argv[1] : "";
  string out_fname = argc > 2 ? argv[2] : "";

  std::unique_ptr<FstClass> ifst(FstClass::Read(in_fname));
  if (!ifst) return 1;

  std::vector<WeightClass> distance;

  fst::QueueType queue_type;
  if (!s::GetQueueType(FLAGS_queue_type, &queue_type)) {
    LOG(ERROR) << argv[0]
               << ": Unknown or unsupported queue type: " << FLAGS_queue_type;
    return 1;
  }

  if (FLAGS_reverse && queue_type != fst::AUTO_QUEUE) {
    LOG(ERROR) << argv[0] << ": Can't use non-default queue with reverse.";
    return 1;
  }

  if (FLAGS_reverse) {
    s::ShortestDistance(*ifst, &distance, FLAGS_reverse, FLAGS_delta);
  } else {
    s::ShortestDistanceOptions opts(queue_type, s::ANY_ARC_FILTER,
                                    FLAGS_nstate, FLAGS_delta);
    s::ShortestDistance(*ifst, &distance, opts);
  }

  s::WritePotentials(out_fname, distance);

  return 0;
}
