# Test fixtures

Committed input files for the tests, one subdirectory per test module
(`core/`, `io/`, `providers/`, ...). Tests find them through
`mov::test::fixture()` (`tests/support`), which returns an absolute path and
so does not depend on the working directory:

```cpp
#include "mov/test/fixture.hpp"

std::ifstream file{mov::test::fixture("io/imeds/obs.imeds")};
```

- Fixtures are byte-exact data: `.gitattributes` marks this tree `-text` and
  the pre-commit whitespace fixers skip it, so CRLF files stay CRLF.
- Keep fixtures small: the smallest file that exercises the case.
  `check-added-large-files` rejects anything over 1000 KB.
- Name a fixture for what it covers (`float_variable.nc`, `fill_values.nc`),
  not for where it came from.
- libFuzzer seed corpora live here too (`core/parse_version/` seeds
  `fuzz_parse_version`): a few valid and near-valid inputs, not a large corpus.
- The recorded API responses in `docs/provider-apis/` are reference material,
  not fixtures. A provider test copies the minimal piece it needs into
  `providers/<provider>/` and says which recording it came from.
- When a fixture is derived from a legacy file in
  `MetOceanViewer/function_tests/`, say so in the test that uses it.
