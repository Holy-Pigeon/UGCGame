const { argv } = require('puerts');
const bridge = argv.getByName('Bridge');

function getAccessibleInterfaces() {
  const api = {
    Bridge: {
      LogMessage: typeof bridge.LogMessage === 'function',
      EmitGameplayCommand: typeof bridge.EmitGameplayCommand === 'function',
      GetWorldSeconds: typeof bridge.GetWorldSeconds === 'function',
      GetActiveModuleName: typeof bridge.GetActiveModuleName === 'function',
    },
    Notes: [
      '本模块按约束仅使用 Bridge 提供的安全接口。',
      '如需更多能力，请在游戏侧扩展 Bridge 并通过 argv.getByName(\'Bridge\') 暴露。'
    ]
  };
  return api;
}

function printAccessibleInterfaces() {
  const moduleName = (bridge.GetActiveModuleName && bridge.GetActiveModuleName()) || 'UnknownModule';
  const t = (bridge.GetWorldSeconds && bridge.GetWorldSeconds()) || 0;
  const api = getAccessibleInterfaces();
  bridge.LogMessage(`[${moduleName}] 可访问接口(安全白名单) @t=${t}: ` + JSON.stringify(api.Bridge));
}

// Run on module load
printAccessibleInterfaces();

module.exports = { getAccessibleInterfaces, printAccessibleInterfaces };
