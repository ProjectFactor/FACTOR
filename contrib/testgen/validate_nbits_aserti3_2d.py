#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# This program validates that a Python implementation of the ASERT algorithm
# produces identical outputs to test vectors generated from the C++
# implementation in pow.cpp.
#
# The test vectors must be in 'run*' text files in the current directory.
# These run files are produced by the gen_asert_test_vectors program
# which can be built using the Makefile in this folder (you will first
# need to do a build of BCHN itself. See the description in the Makefile.
#
# If arguments are given, they must be the names of run* files in the
# current directory.
# If no arguments are given, the list of run* files is determined by
# inspecting the current directory and all such files are processed.

import os
import sys

# Parameters needed by ASERT
IDEAL_BLOCK_TIME = 10 * 60
HALFLIFE = 2 * 24 * 3600
# Integer implementation uses these for fixed point math
RBITS = 16      # number of bits after the radix for fixed-point math
RADIX = 1 << RBITS
# POW Limit
MAX_BITS = 0x1d00ffff


def bits_to_target(bits):
    size = bits >> 24
    assert size <= 0x1d

    word = bits & 0x00ffffff
    assert 0x8000 <= word <= 0x7fffff

    if size <= 3:
        return word >> (8 * (3 - size))
    else:
        return word << (8 * (size - 3))


MAX_TARGET = bits_to_target(MAX_BITS)


def target_to_bits(target):
    assert target > 0
    if target > MAX_TARGET:
        print('Warning: target went above maximum ({} > {})'.format(target, MAX_TARGET))
        target = MAX_TARGET
    size = (target.bit_length() + 7) // 8
    mask64 = 0xffffffffffffffff
    if size <= 3:
        compact = (target & mask64) << (8 * (3 - size))
    else:
        compact = (target >> (8 * (size - 3))) & mask64

    if compact & 0x00800000:
        compact >>= 8
        size += 1

    assert compact == (compact & 0x007fffff)
    assert size < 256
    return compact | size << 24

def bits_to_work(bits):
    return (2 << 255) // (bits_to_target(bits) + 1)

def target_to_hex(target):
    h = hex(target)[2:]
    return '0' * (64 - len(h)) + h

def next_bits_aserti3_2d(anchor_bits, time_diff, height_diff):
    ''' Integer ASERTI algorithm, based on Jonathan Toomim's `next_bits_aserti`
    implementation in mining.py (see https://github.com/jtoomim/difficulty)'''

    target = bits_to_target(anchor_bits)

    # Ultimately, we want to approximate the following ASERT formula, using only integer (fixed-point) math:
    #     new_target = old_target * 2^((time_diff - IDEAL_BLOCK_TIME*(height_diff+1)) / HALFLIFE)

    # First, we'll calculate the exponent, using floor division.
    exponent = int(((time_diff - IDEAL_BLOCK_TIME*(height_diff+1)) * RADIX) / HALFLIFE)

    # Next, we use the 2^x = 2 * 2^(x-1) identity to shift our exponent into the (0, 1] interval.
    shifts = exponent >> RBITS
    exponent -= shifts*RADIX
    assert(exponent >= 0 and exponent < 65536)

    # Now we compute an approximated target * 2^(fractional part) * 65536
    # target * 2^x ~= target * (1 + 0.695502049*x + 0.2262698*x**2 + 0.0782318*x**3)
    target *= RADIX + ((195766423245049*exponent + 971821376*exponent**2 + 5127*exponent**3 + 2**47)>>(RBITS*3))

    # Next, we shift to multiply by 2^(integer part). Python doesn't allow shifting by negative integers, so:
    if shifts < 0:
        target >>= -shifts
    else:
        target <<= shifts
    # Remove the 65536 multiplier we got earlier
    target >>= RBITS

    if target == 0:
        return target_to_bits(1)
    if target > MAX_TARGET:
        return MAX_BITS

    return target_to_bits(target)


def check_run_file(run_file_path):
    '''Reads and validates a single run file.'''

    run = open(run_file_path, 'r')

    # Initialize these to invalid values to catch their absence
    anchor_height = 0
    anchor_time = -1
    iterations = 0

    iteration_counter = 1
    for l in run:
        l = l.strip()
        if l.startswith('## description:'):
            print(l)
        elif l == '':
            pass
        elif l.startswith('##   anchor height: '):  # height of anchor block
            anchor_height = int(l[19:])
            assert anchor_height > 0, "Unexpected anchor height value '{}' is <= 0 in header of {}".format(anchor_height, run_file_path)
        elif l.startswith('##   anchor parent time: '):    # timestamp of anchor block's parent, in seconds
            anchor_time = int(l[24:])
            assert anchor_time >= 0, "Unexpected anchor time value '{}' is < 0 in header of {}".format(anchor_time, run_file_path)
        elif l.startswith('##   iterations: '):  # number of iterations expected in this run file
            iterations = int(l[17:])
            assert iterations > 0, "Unexpected iterations value '{}' is <= 0 in header of {}".format(iterations, run_file_path)
        elif l.startswith('##   anchor nBits: '):   # anchor block nBits (next target) value
            anchor_nbits = l[19:]
            # Sanity check: convert to integer target and back to bits and assert that it remains unchanged
            anchor_nbits_int = int(anchor_nbits, 16)
            target = bits_to_target(anchor_nbits_int)
            bits = target_to_bits(target)
            bits_str = "0x{:08x}".format(bits)
            assert bits_str == anchor_nbits, "Unexpected anchor nBits that did not convert to target and back identically: {}".format(anchor_nbits)
        elif not l.startswith('#'):              # this should be an iteration
            # Check we have the basic necessities from header
            assert anchor_height > 0, "Something is wrong in {} - no valid anchor height".format(run_file_path)
            assert anchor_time > -1, "Something is wrong in {} - no valid anchor time".format(run_file_path)
            assert iterations > 0, "Something is wrong in {} - no valid number of iterations".format(run_file_path)

            split_l = l.split(' ')
            #print(split_l)
            (it, height, time_secs, next_nbits_from_file) = [int(split_l[0]), int(split_l[1]), int(split_l[2]), split_l[3]] 
            assert it == iteration_counter, "Unexpected iteration counter '{}' in {}".format(it, run_file_path)
            assert it <= iterations, "Number of iterations in {} exceeds header specifications".format(run_file_path)

            print("next_bits_aserti3_2d({}, {}, {})".format(int(anchor_nbits, 16), time_secs - anchor_time, height - anchor_height))
            calculated_nbits = next_bits_aserti3_2d(int(anchor_nbits, 16), time_secs - anchor_time, height - anchor_height)
            calculated_nbits_str = "0x{:08x}".format(calculated_nbits)
            assert calculated_nbits_str == next_nbits_from_file, "Target mismatch ({} instead of {} in iteration {} in {}".format(calculated_nbits_str, next_nbits_from_file, it, run_file_path)
            iteration_counter += 1
    run.close()
    # Assert that no iterations were missing from the file
    assert iteration_counter == iterations + 1
    print("OK")


def main():
    '''Reads a run file generated by gen_asert_test_vectors and
       runs the Python ASERT against it to check that the calculated
       nBits are the same as for the C++ implementation.'''
    if len(sys.argv) == 1:
        run_files = sorted([i for i in os.listdir('.') if (i.startswith("run"))])
    elif len(sys.argv) > 1:
        run_files = sorted(sys.argv[1:])

    if not run_files:
        print("No run files (test vectors) found!")
        print("Look into building and running the gen_asert_test_vectors program.")
        sys.exit(1)

    for rf in run_files:
        print("Checking run file {}".format(rf))
        # The call below will throw an assertion and halt if anything does not validate.
        check_run_file(rf)
    print("\nAll OK.")
    print("This does not mean the Python aserti3_2d implementation is 100% conformant -\n"
          "it means the test vectors do not show up a difference.")

if __name__ == '__main__':
    main()
