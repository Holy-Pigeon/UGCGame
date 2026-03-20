const { argv } = require('puerts');
const bridge = argv.getByName('Bridge');

function emitHello() {
  const payload = {
    module: bridge.GetActiveModuleName(),
    t: bridge.GetWorldSeconds(),
    text: '你好',
  };
  bridge.LogMessage(`[AIHotfix] ${payload.text} (module=${payload.module}, t=${payload.t.toFixed(3)})`);
  bridge.EmitGameplayCommand('AIHotfix.Hello', JSON.stringify(payload));
}

// Run on load
emitHello();

module.exports = { emitHello };
