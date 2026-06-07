# Historical Projects

The old top-level projects were read before restructuring. Their active lessons
were captured here, then the projects were removed from the buildable tree.

## Basic_linear_probing

This was a small educational fixed-size linear probing program. It used a global
array, integer keys/values, a tombstone flag, and an interactive command-line
flow. Its functionality is superseded by the modern `open_addressing` backend.

Retained lesson: keep the baseline linear-probing implementation simple enough
to serve as a reference point for more advanced open-addressing variants.

## General_Open_Addressing

This project experimented with a generic `void *` open-addressing API, callback
hash/comparison/probe functions, separate probe strategies, and benchmark CSVs
for hash/probe combinations. Its Makefile depended on external Unity paths and
its benchmark tooling lived separately from the modern benchmark CLI.

Retained lesson: probe strategy and hash choice matter, but the active project
uses a unified fixed-key benchmark API so every retained backend can be compared
under one runner.

## Open_Table

This project explored a generic open table API with multiple C source versions,
Unity tests, a Google Benchmark C++ benchmark, generated build artifacts, and a
gnuplot script. It overlapped heavily with the modern API and build/benchmark
goals.

Retained lesson: versioned implementation files are less useful than one
central test and benchmark matrix that can compare named backend variants.

## Removed Dependencies And Artifacts

The active repo no longer keeps external Unity, Google Benchmark, notebook,
plotting, or archived-result tooling. Historical benchmark artifacts were not
kept as source of truth because the modern benchmark CLI can regenerate current
measurements under one configuration.
