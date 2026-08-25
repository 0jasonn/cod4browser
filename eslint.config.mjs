export default [{
    files: [
        "web/product_*.mjs",
        "web/asset_profile.mjs",
        "tests/node/*.mjs",
    ],
    languageOptions: { ecmaVersion: 2024, sourceType: "module" },
    linterOptions: { reportUnusedDisableDirectives: "error" },
    rules: {
        "eqeqeq": "error",
        "no-constant-binary-expression": "error",
        "no-duplicate-imports": "error",
        "no-unused-vars": ["error", { argsIgnorePattern: "^_", caughtErrors: "none" }],
    },
}];
