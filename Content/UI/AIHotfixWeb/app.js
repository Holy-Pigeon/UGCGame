(function () {
  const elements = {
    token: document.getElementById("token"),
    model: document.getElementById("model"),
    module: document.getElementById("module"),
    prompt: document.getElementById("prompt"),
    login: document.getElementById("login"),
    generate: document.getElementById("generate"),
    reload: document.getElementById("reload"),
    authSummary: document.getElementById("auth-summary"),
    authDetail: document.getElementById("auth-detail"),
    authDot: document.getElementById("auth-dot"),
    authHint: document.getElementById("auth-hint"),
    authTopPill: document.getElementById("auth-top-pill"),
    statusLabel: document.getElementById("status-label"),
    statusCaption: document.getElementById("status-caption"),
    reloadPill: document.getElementById("reload-pill"),
    messages: document.getElementById("messages"),
    messageCount: document.getElementById("message-count"),
    runtimeLog: document.getElementById("runtime-log"),
    generateCaption: document.getElementById("generate-caption"),
    starters: Array.from(document.querySelectorAll(".starter"))
  };

  const draft = {
    token: "",
    model: "gpt-5.2",
    moduleName: "AIHotfix/Generated/Current",
    prompt: ""
  };

  function getBridge() {
    if (!window.ue || !window.ue.bridge) {
      return null;
    }

    return window.ue.bridge;
  }

  function callBridge(methodName, args) {
    const bridge = getBridge();
    if (!bridge || typeof bridge[methodName] !== "function") {
      return false;
    }

    bridge[methodName].apply(bridge, args || []);
    return true;
  }

  function updateDraftFromInputs() {
    draft.token = elements.token.value;
    draft.model = elements.model.value || "gpt-5.2";
    draft.moduleName = elements.module.value;
    draft.prompt = elements.prompt.value;
  }

  function pushDraft() {
    updateDraftFromInputs();
    callBridge("updatedraft", [draft.token, draft.model, draft.moduleName, draft.prompt]);
  }

  function createMessageNode(message) {
    const role = (message.role || "").toLowerCase();
    const kind = message.kind || "message";
    const title = message.title || "";
    const isUser = role === "user";
    const isTool = kind === "tool_call" || kind === "tool_result";
    const wrapper = document.createElement("div");
    wrapper.className =
      "message " +
      (isUser ? "user" : (isTool ? "tool" : "assistant")) +
      " kind-" + kind;

    const avatar = document.createElement("div");
    avatar.className = "avatar";
    avatar.textContent = isUser ? "YOU" : (isTool ? "TOOL" : "AI");

    const bubble = document.createElement("div");
    bubble.className = "bubble";

    const roleLabel = document.createElement("div");
    roleLabel.className = "message-role";
    if (isTool) {
      roleLabel.textContent = (kind === "tool_call" ? "Tool Call" : "Tool Result") + (title ? " · " + title : "");
    } else {
      roleLabel.textContent = isUser ? "You" : (role === "assistant" ? "Assistant" : (message.role || "System"));
    }

    const content = document.createElement("div");
    content.textContent = message.content || "";

    bubble.appendChild(roleLabel);
    bubble.appendChild(content);
    wrapper.appendChild(avatar);
    wrapper.appendChild(bubble);
    return wrapper;
  }

  function renderMessages(messages) {
    elements.messages.innerHTML = "";

    if (!messages || messages.length === 0) {
      const welcome = document.createElement("div");
      welcome.className = "welcome";
      welcome.innerHTML = [
        '<div class="welcome-badge">Chat First</div>',
        '<h3>先描述你想改的玩法或表现</h3>',
        '<p>AI 会先在对话区回应摘要和建议。生成热更、试运行、热重载都退到第二层操作。</p>'
      ].join("");

      const starterGrid = document.createElement("div");
      starterGrid.className = "starter-grid";
      [
        "让角色冲刺时带一点镜头前冲和轻微 FOV 变化，但不要头晕。",
        "做一个更有重量感的受击反馈，要求带短暂停顿、镜头震动和材质闪白。",
        "把当前交互提示改得更清楚，靠近物体时给出更明确的操作提示和高亮。"
      ].forEach((promptText, index) => {
        const button = document.createElement("button");
        button.className = "starter";
        button.type = "button";
        button.dataset.prompt = promptText;
        button.textContent = ["冲刺手感优化", "受击反馈优化", "交互提示优化"][index];
        button.addEventListener("click", () => {
          elements.prompt.value = promptText;
          pushDraft();
          elements.prompt.focus();
        });
        starterGrid.appendChild(button);
      });

      welcome.appendChild(starterGrid);
      elements.messages.appendChild(welcome);
      elements.messageCount.textContent = "0 messages";
      return;
    }

    messages.forEach((message) => {
      elements.messages.appendChild(createMessageNode(message));
    });

    elements.messageCount.textContent = messages.length + (messages.length === 1 ? " message" : " messages");
    elements.messages.scrollTop = elements.messages.scrollHeight;
  }

  function renderRuntime(runtimeLines) {
    if (!runtimeLines || runtimeLines.length === 0) {
      elements.runtimeLog.textContent = "No runtime events yet.";
      return;
    }

    elements.runtimeLog.textContent = runtimeLines.join("\n");
    elements.runtimeLog.scrollTop = elements.runtimeLog.scrollHeight;
  }

  function renderAuth(auth) {
    const detailParts = [];
    if (auth.statusMessage) {
      detailParts.push(auth.statusMessage);
    }
    if (auth.userCode) {
      detailParts.push("Code: " + auth.userCode);
    }
    if (auth.verificationUri) {
      detailParts.push("URL: " + auth.verificationUri);
    }

    const summary = auth.summary || "GitHub not connected";
    const detail = detailParts.join(" ") || "Paste a token or start device login.";
    elements.authSummary.textContent = summary;
    elements.authDetail.textContent = detail;
    elements.authHint.textContent = detailParts.join("\n") || "Copilot auth: token paste or device login";
    elements.authTopPill.textContent = summary;
    elements.authDot.className = "status-dot" + (auth.isAuthenticated ? " ready" : (auth.isPending ? " pending" : ""));
  }

  function renderStatus(state) {
    elements.statusLabel.textContent = state.statusText || "Ready";
    elements.statusCaption.textContent = state.isGenerating
      ? "正在等待模型回复或执行工具。"
      : (state.hasPendingHotfix ? "已经有一份待热重载的生成结果。" : "先继续聊天，把需求说完整。");
    elements.reloadPill.textContent = state.hasPendingHotfix ? "Pending hotfix ready" : "No pending hotfix";
    elements.generateCaption.textContent = state.isGenerating ? "Agent running" : "Ready";
    elements.generate.textContent = state.isGenerating ? "Thinking..." : "Send";
    elements.generate.disabled = !!state.isGenerating;
    elements.reload.disabled = !state.hasPendingHotfix;
  }

  function sendPrompt() {
    if (!elements.prompt.value.trim()) {
      elements.prompt.focus();
      return;
    }
    pushDraft();
    callBridge("send", [draft.token, draft.model, draft.moduleName, draft.prompt]);
  }

  window.ugcHotfix = {
    applyState(state) {
      if (state.hydrateInputs && state.draft) {
        draft.token = state.draft.token || "";
        draft.model = state.draft.model || "gpt-5.2";
        draft.moduleName = state.draft.moduleName || "AIHotfix/Generated/Current";
        draft.prompt = state.draft.prompt || "";

        elements.token.value = draft.token;
        elements.model.value = draft.model;
        elements.module.value = draft.moduleName;
        elements.prompt.value = draft.prompt;
      }

      renderMessages(state.messages || []);
      renderRuntime(state.runtimeLines || []);
      renderAuth(state.auth || {});
      renderStatus(state);
    }
  };

  elements.login.addEventListener("click", () => {
    pushDraft();
    callBridge("login", [draft.token, draft.model, draft.moduleName, draft.prompt]);
  });

  elements.generate.addEventListener("click", () => {
    sendPrompt();
  });

  elements.reload.addEventListener("click", () => {
    pushDraft();
    callBridge("reload");
  });

  elements.prompt.addEventListener("keydown", (event) => {
    if ((event.metaKey || event.ctrlKey) && event.key === "Enter") {
      event.preventDefault();
      sendPrompt();
    }
  });

  ["change", "blur"].forEach((eventName) => {
    elements.token.addEventListener(eventName, pushDraft);
    elements.model.addEventListener(eventName, pushDraft);
    elements.module.addEventListener(eventName, pushDraft);
    elements.prompt.addEventListener(eventName, pushDraft);
  });

  elements.starters.forEach((button) => {
    button.addEventListener("click", () => {
      elements.prompt.value = button.dataset.prompt || "";
      pushDraft();
      elements.prompt.focus();
    });
  });

  function notifyReady() {
    pushDraft();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", notifyReady, { once: true });
  } else {
    notifyReady();
  }

  document.addEventListener("ue:ready", notifyReady);
  document.addEventListener("bridge:ready", notifyReady);
})();
