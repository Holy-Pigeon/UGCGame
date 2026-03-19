const { argv } = require("puerts");

const bridge = argv.getByName("Bridge");

bridge.LogMessage("Sample hotfix booted");
bridge.EmitGameplayCommand(
  "ShowToast",
  JSON.stringify({
    text: "AI hotfix is live",
    atSeconds: bridge.GetWorldSeconds(),
    module: bridge.GetActiveModuleName(),
  })
);
