# Testing Patterns

## Core Sections (Required)

### 1) Test Stack and Commands

- **Primary test framework**: None — no test framework is configured or present
- **Assertion/mocking tools**: None
- Commands: N/A

```bash
# No test commands exist
```

### 2) Test Layout

- No test files exist anywhere in the repository
- No `tests/`, `test/`, or `__tests__/` directories
- [TODO] No documented test layout policy

### 3) Test Scope Matrix

| Scope | Covered? | Typical target | Notes |
|-------|----------|----------------|-------|
| Unit | No | — | No unit tests exist |
| Integration | No | — | No integration tests exist |
| E2E | No | — | No E2E tests exist |

The plugin is a VCV Rack module that depends on the Rack runtime for all meaningful execution. Testing in isolation requires either mocking the Rack API or running inside VCV Rack itself, which raises the barrier to adding automated tests.

### 4) Mocking and Isolation Strategy

- No mocking strategy exists
- [ASK USER] Is there a planned approach for testing? (e.g., mock the Rack API, test bundlers in isolation, integration tests against a headless Rack instance)

### 5) Coverage and Quality Signals

- Coverage tool: None
- Coverage threshold: None
- Current reported coverage: 0%
- Known gaps: All production code is untested

### 6) Evidence

- Scan output: no test directories found
- `Makefile`: no `test` target
- `.github/workflows/build-plugin.yml`: no test step
