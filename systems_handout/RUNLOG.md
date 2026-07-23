# Performance Log

## Delay Tuning Results - Profile A (Verification Run)
*Network characteristics: Lower loss/delay.*

| Profile | Delay (ms) | Miss Rate (%) | Overhead | Result  | Notes                                      |
|---------|------------|---------------|----------|---------|--------------------------------------------|
| A       | 150        | 0.60% (9)     | 1.90x    | VALID   | Baseline run.                              |
| A       | 120        | 0.87% (13)    | 1.90x    | VALID   | Safely under 1% cap.                       |
| A       | 100        | 0.73% (11)    | 1.90x    | VALID   | Holds steady under the cap.                |
| A       | 80         | 0.87% (13)    | 1.90x    | VALID   | Absolute floor for Profile A before spike. |
| A       | 60         | 10.93% (164)  | 1.90x    | INVALID | Jitter cliff hit. Massive miss spike.      |

---

## Delay Tuning Results - Profile B (Verification Run)
*Network characteristics: `loss`: 0.05 (5%), `delay_max_ms`: 80.*

| Profile | Delay (ms) | Miss Rate (%) | Overhead | Result  | Notes                                                    |
|---------|------------|---------------|----------|---------|-----------------------------------------------------------|
| B       | 100        | 7.40% (111)   | 1.90x    | INVALID | Failed. Buffer isn't long enough to wait for redundancy. |
| B       | 80         | 38.40% (576)  | 1.90x    | INVALID | Massive failure. Equal to network's max delay.           |

---

## Miss Sequence Analysis & Diagnosis

**Root Cause (confirmed against sender.c / receiver.c):**
* The sender always attaches the payload from frame `i-2` as the redundant copy (`prev2_payload`), never `i-1`. It also skips attaching any redundant copy at all on every 32nd frame (`seq % 32 == 0`).
* The receiver, however, assumes the redundant copy it receives belongs to frame `i-1`, and writes it into `idx_prev = (seq - 1) % BUFFER_SIZE`. This is a mismatch: it should be `(seq - 2) % BUFFER_SIZE` to match what the sender actually sends.
* Net effect: frame `m`'s only real backup is packet `m+2`, and even when that packet arrives, the receiver files its payload under the wrong slot. If frame `m-1` was genuinely lost, its slot can end up incorrectly filled with frame `m-2`'s data (corruption, not a clean recovery), and frame `m` itself gets no protection if packet `m+2` is lost or falls on the modulo-32 skip.
* The earlier "misses cluster at `seq % 8 == 7`" theory does not hold up on closer inspection of the listed sequence numbers (e.g. 1075 and 1199 aren't consistently `≡ 7 mod 8`) and is superseded by this root cause.
* The trailing wall of misses at the end of the stream (1499-1529) is a separate, expected effect of the receiver continuing to expect frames after the sender has stopped.

**Profile B Insights (Consistent across runs):**
* Profile B drops 81 packets compared to Profile A's 34.
* Because `delay_max_ms` is 80, if a packet is lost, the redundant copy attached to a future packet might also be delayed by up to 80ms. At a `delay_ms` of 100, there is only 20ms of slack to receive that backup packet — insufficient given the 5% drop rate, and made worse by the offset mismatch above reducing effective redundancy.

**Required Action:**
Tuning `delay_ms` alone is not sufficient. The receiver's recovery index must be fixed to `idx_prev = (seq - 2) % BUFFER_SIZE` so it matches what the sender actually transmits. Once frames get their intended two-frame-deep backup instead of a mislabeled one, re-run both profiles to see whether `delay_ms` can be lowered while still passing Profile B.