# AGENTS.md

- Read `TODO.md` before changing Gaussian, Sobol, AVX-512, or European pricer
  code.
- Do not touch the normal/good Gaussian path unless explicitly requested.
- Tail experiments should start as report/tooling work before assembly changes.
- Avoid unnecessary hot-loop branches; prefer deterministic schedules, masks,
  and compact data layouts.
