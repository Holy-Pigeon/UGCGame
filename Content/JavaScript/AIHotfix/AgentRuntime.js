// Resident agent runtime. Booted by UGCAIHotReloadSubsystem on startup and on
// every hotfix reload. Provides the run_js eval endpoint for the agent kernel
// and loads the active hotfix module (if any).
const { argv } = require('puerts');

const bridge = argv.getByName('Bridge');
if (!bridge) {
    throw new Error('AgentRuntime: Bridge argument is missing');
}

function describeValue(value, depth) {
    if (value === undefined) return 'undefined';
    if (value === null) return 'null';
    const type = typeof value;
    if (type === 'string') return value;
    if (type === 'number' || type === 'boolean') return String(value);
    if (type === 'function') return '[function ' + (value.name || 'anonymous') + ']';
    try {
        return JSON.stringify(value, function (key, item) {
            if (typeof item === 'function') return '[function]';
            return item;
        }, depth > 0 ? 2 : 0);
    } catch (e) {
        return String(value);
    }
}

// run_js entry point: evaluate code, capture log() output and the completion
// value, and hand a JSON report back to C++.
bridge.ScriptEvalHandler.Bind(function (code) {
    const logs = [];
    // Give evaluated snippets a convenient logger without clobbering globals.
    globalThis.log = function () {
        const parts = [];
        for (let i = 0; i < arguments.length; ++i) {
            parts.push(describeValue(arguments[i], 1));
        }
        logs.push(parts.join(' '));
    };

    try {
        const result = eval(code);
        const report = { ok: true, result: describeValue(result, 1) };
        if (logs.length > 0) report.logs = logs;
        return JSON.stringify(report);
    } catch (e) {
        const report = { ok: false, error: String((e && e.stack) || e) };
        if (logs.length > 0) report.logs = logs;
        return JSON.stringify(report);
    } finally {
        delete globalThis.log;
    }
});

bridge.LogMessage('AgentRuntime ready (run_js available).');

// Load the active hotfix module, if one has been generated/applied.
const hotfixModule = bridge.GetHotfixModuleName();
if (hotfixModule) {
    try {
        require(hotfixModule);
        bridge.LogMessage('Loaded hotfix module: ' + hotfixModule);
    } catch (e) {
        bridge.LogMessage('Hotfix module failed to load (' + hotfixModule + '): ' + String((e && e.stack) || e));
    }
}
