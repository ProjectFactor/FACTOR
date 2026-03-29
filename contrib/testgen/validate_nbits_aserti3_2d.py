#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin developers
# Copyright (c) 2026 The FACTOR developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# This program validates that a Python implementation of the FACTOR ASERT
# algorithm produces identical outputs to test vectors generated from the
# C++ implementation in pow.cpp.
#
# Usage:
#   python3 validate_nbits_aserti3_2d.py <vector_file>
#   python3 validate_nbits_aserti3_2d.py run*
#
# If no arguments are given, processes all run* files in the current directory.

import os
import re
import sys


def load_table(header_path: str) -> list[int]:
    """Parse LOG2_COMPUTE_TABLE from src/asert_table.h"""
    table: list[int] = []
    with open(header_path, "r") as f:
        in_array = False
        for line in f:
            if "LOG2_COMPUTE_TABLE" in line and "{" in line:
                in_array = True
            if not in_array:
                continue
            # Match entries like "  9087915284LL,"
            for m in re.finditer(r"(-?\d+)LL", line):
                table.append(int(m.group(1)))
            if "};" in line:
                break
    assert len(table) == 511, "Expected 511 table entries, got %d" % len(table)
    return table


def calculate_factor_asert(
    table: list[int],
    target_spacing: int,
    half_life: int,
    nbits_min: int,
    nbits_max: int,
    anchor_nbits: int,
    time_diff: int,
    height_diff: int,
) -> tuple[int, bool]:
    """Pure-Python implementation of CalculateFACTORASERT.

    Mirrors the C++ implementation exactly:
    - Q32.32 fixed-point exponent via overflow-safe split division
    - Binary search for closest table entry
    - Tiebreak: lower nBits (easier difficulty)
    """
    assert anchor_nbits % 2 == 0
    assert nbits_min <= anchor_nbits <= nbits_max
    assert height_diff >= 0

    # Step 1: Look up anchor's log2(compute)
    anchor_idx = anchor_nbits // 2 - 1
    anchor_log2_compute = table[anchor_idx]

    # Step 2: Compute exponent in Q32.32
    expected_elapsed = height_diff * target_spacing
    time_error = expected_elapsed - time_diff

    # Overflow-safe Q32.32 split division (matches C++)
    # Python handles arbitrary precision, but we replicate the C++ truncation
    # toward zero for division of negative numbers.
    if time_error >= 0:
        integer_part = time_error // half_life
        remainder = time_error % half_life
    else:
        # C++ truncates toward zero: -7 / 3 == -2, -7 % 3 == -1
        integer_part = -((-time_error) // half_life)
        remainder = -((-time_error) % half_life)

    exponent_q32 = (integer_part << 32) + ((remainder << 32) // half_life)

    # Step 3: target log2_compute
    target_log2_compute = anchor_log2_compute + exponent_q32

    # Step 4: Binary search
    idx_min = nbits_min // 2 - 1
    idx_max = nbits_max // 2 - 1

    clamped_high = target_log2_compute > table[idx_max]

    if target_log2_compute <= table[idx_min]:
        result_idx = idx_min
    elif target_log2_compute >= table[idx_max]:
        result_idx = idx_max
    else:
        lo, hi = idx_min, idx_max
        while lo + 1 < hi:
            mid = lo + (hi - lo) // 2
            if table[mid] <= target_log2_compute:
                lo = mid
            else:
                hi = mid
        dist_lo = target_log2_compute - table[lo]
        dist_hi = table[hi] - target_log2_compute
        result_idx = lo if dist_lo <= dist_hi else hi

    new_nbits = (result_idx + 1) * 2
    return new_nbits, clamped_high


def check_run_file(run_file_path: str, table: list[int]) -> None:
    """Reads and validates run(s) in a file against Python ASERT.

    A file may contain multiple runs separated by blank lines.
    Each run has its own header (## lines) and data lines.
    """

    anchor_height = None
    anchor_time = None
    anchor_nbits = None
    target_spacing = None
    half_life = None
    nbits_min = None
    nbits_max = None
    iterations = None
    iteration_counter = 1
    runs_checked = 0

    def finalize_run():
        nonlocal runs_checked
        if iterations is not None and iterations > 0:
            assert iteration_counter == iterations + 1, (
                "Expected %d iterations but found %d in run %d of %s"
                % (iterations, iteration_counter - 1, runs_checked + 1, run_file_path)
            )
            runs_checked += 1
            print("  OK (%d iterations)" % iterations)

    with open(run_file_path, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("## description:"):
                # New run starting — finalize the previous one if any
                if iterations is not None:
                    finalize_run()
                # Reset state for new run
                anchor_height = None
                anchor_time = None
                anchor_nbits = None
                target_spacing = None
                half_life = None
                nbits_min = None
                nbits_max = None
                iterations = None
                iteration_counter = 1
                print(line)
            elif line == "":
                pass
            elif line.startswith("##   anchor height: "):
                anchor_height = int(line.split(": ", 1)[1])
            elif line.startswith("##   anchor time: "):
                anchor_time = int(line.split(": ", 1)[1])
            elif line.startswith("##   anchor nBits: "):
                anchor_nbits = int(line.split(": ", 1)[1])
            elif line.startswith("##   target spacing: "):
                target_spacing = int(line.split(": ", 1)[1])
            elif line.startswith("##   half life: "):
                half_life = int(line.split(": ", 1)[1])
            elif line.startswith("##   nBitsMin: "):
                parts = line.split()
                nbits_min = int(parts[2])
                nbits_max = int(parts[4])
            elif line.startswith("##   iterations: "):
                iterations = int(line.split(": ", 1)[1])
            elif line.startswith("##") or line.startswith("#"):
                pass
            else:
                assert anchor_height is not None, (
                    "Missing anchor height in %s" % run_file_path
                )
                assert iterations is not None, (
                    "Missing iterations in %s" % run_file_path
                )

                parts = line.split()
                it = int(parts[0])
                height = int(parts[1])
                time_secs = int(parts[2])
                nbits_from_file = int(parts[3])
                clamped_from_file = int(parts[4]) != 0

                assert it == iteration_counter, (
                    "Unexpected iteration %d (expected %d) in run %d of %s"
                    % (it, iteration_counter, runs_checked + 1, run_file_path)
                )
                assert anchor_time is not None, (
                    "Missing anchor time in %s" % run_file_path
                )
                assert target_spacing is not None, (
                    "Missing target spacing in %s" % run_file_path
                )
                assert half_life is not None, (
                    "Missing half life in %s" % run_file_path
                )
                assert nbits_min is not None, (
                    "Missing nbits min in %s" % run_file_path
                )
                assert nbits_max is not None, (
                    "Missing nbits max in %s" % run_file_path
                )
                assert anchor_nbits is not None, (
                    "Missing anchor nbits in %s" % run_file_path
                )

                time_diff = time_secs - anchor_time
                height_diff = height - anchor_height

                calc_nbits, calc_clamped = calculate_factor_asert(
                    table,
                    target_spacing,
                    half_life,
                    nbits_min,
                    nbits_max,
                    anchor_nbits,
                    time_diff,
                    height_diff,
                )

                assert calc_nbits == nbits_from_file, (
                    "nBits mismatch: Python=%d, C++=%d at iteration %d in %s"
                    % (calc_nbits, nbits_from_file, it, run_file_path)
                )
                assert calc_clamped == clamped_from_file, (
                    "clampedHigh mismatch: Python=%s, C++=%s at iteration %d in %s"
                    % (calc_clamped, clamped_from_file, it, run_file_path)
                )

                iteration_counter += 1

    # Finalize the last run
    finalize_run()
    print("Checked %d runs" % runs_checked)


def main():
    # Locate the lookup table header relative to this script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    table_path = os.path.join(script_dir, "..", "..", "src", "asert_table.h")
    table_path = os.path.normpath(table_path)

    if not os.path.exists(table_path):
        print("Cannot find %s" % table_path)
        sys.exit(1)

    table = load_table(table_path)
    print("Loaded %d-entry lookup table from %s" % (len(table), table_path))

    if len(sys.argv) > 1:
        run_files = sorted(sys.argv[1:])
    else:
        run_files = sorted(f for f in os.listdir(".") if f.startswith("run"))

    if not run_files:
        print("No run files (test vectors) found!")
        print(
            "Generate them with: src/contrib/testgen/gen_asert_test_vectors > vectors.txt"
        )
        sys.exit(1)

    for rf in run_files:
        print("\nChecking %s" % rf)
        check_run_file(rf, table)

    print("\nAll OK.")
    print(
        "This confirms the Python FACTOR ASERT implementation matches "
        + "the C++ implementation for all test vectors."
    )


if __name__ == "__main__":
    main()
