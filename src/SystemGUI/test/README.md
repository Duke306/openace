# Frontend Unit Tests

This directory is reserved for SystemGUI frontend unit tests.

## Run tests

From `src/SystemGUI`:

```bash
npm test
```

## File naming

Use `*.test.mjs` files so they are picked up by the `npm test` script.

## Notes

- The default setup uses Node's built-in test runner.
- For browser/DOM-specific component tests, add a DOM-capable test stack (for example Vitest + jsdom) in a follow-up change.
