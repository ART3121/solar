## Summary

Describe the user-visible outcome and the Core/service/backend boundary changed.

## Verification

List exact build, focused-test, full-CTest, sanitizer, and real-tool commands.
Separate passes from skipped external integrations.

## Checklist

- [ ] The patch is focused and preserves unrelated user changes.
- [ ] Reusable behavior is in Core; CLI code remains a thin adapter.
- [ ] Backend-specific argv and output interpretation remain isolated.
- [ ] No shell command string, automatic download, or unsafe deletion was added.
- [ ] Behavior changes have normal and failure-path regressions.
- [ ] Public documentation and the implementation plan match the code.
- [ ] Ownership, cleanup, diagnostics, and compatibility were reviewed.
