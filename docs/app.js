(function () {
  "use strict";

  var storageKey = "sqlitercs-playground-v1";
  var state = loadState();
  var selectedRevision = "";
  var showHistory = false;
  var toastTimer;
  var elements = {
    content: document.getElementById("content-input"),
    log: document.getElementById("log-input"),
    checkinTarget: document.getElementById("checkin-target"),
    checkin: document.getElementById("checkin"),
    branchParent: document.getElementById("branch-parent"),
    branchName: document.getElementById("branch-name"),
    createBranch: document.getElementById("create-branch"),
    revisions: document.getElementById("revision-list"),
    revisionCount: document.getElementById("revision-count"),
    checkoutRevision: document.getElementById("checkout-revision"),
    checkout: document.getElementById("checkout"),
    diffRevision: document.getElementById("diff-revision"),
    showDiff: document.getElementById("show-diff"),
    result: document.getElementById("result-content"),
    resultKind: document.getElementById("result-kind"),
    message: document.getElementById("form-message"),
    toggleHistory: document.getElementById("toggle-history"),
    historyPreview: document.getElementById("history-preview"),
    download: document.getElementById("download-history"),
    reset: document.getElementById("reset-demo"),
    toast: document.getElementById("toast")
  };

  function loadState() {
    try {
      var saved = JSON.parse(localStorage.getItem(storageKey) || "null");
      if (saved && Array.isArray(saved.revisions) && Array.isArray(saved.branches) &&
          Array.isArray(saved.tags)) {
        return saved;
      }
    } catch (error) {
      return { revisions: [], branches: [], tags: [], content: "" };
    }
    return { revisions: [], branches: [], tags: [], content: "" };
  }

  function persist() {
    try {
      localStorage.setItem(storageKey, JSON.stringify(state));
      return true;
    } catch (error) {
      return false;
    }
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, function (character) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[character];
    });
  }

  function revisionById(id) {
    return state.revisions.find(function (revision) { return revision.id === id; });
  }

  function revisionLabel(revision) {
    return revision.id + (revision.branch ? " · " + revision.branch : "");
  }

  function orderedRevisions() {
    return state.revisions.slice().reverse();
  }

  function branchTip(name) {
    var branch = state.branches.find(function (item) { return item.name === name; });
    if (!branch) return null;
    var revisions = state.revisions.filter(function (revision) {
      return revision.branch === name;
    });
    return revisions.length ? revisions[revisions.length - 1] : revisionById(branch.parentRevision);
  }

  function fillSelect(select, options, placeholder) {
    var oldValue = select.value;
    select.innerHTML = "";
    if (!options.length) {
      var empty = document.createElement("option");
      empty.value = "";
      empty.textContent = placeholder;
      select.appendChild(empty);
      select.disabled = true;
      return;
    }
    select.disabled = false;
    options.forEach(function (item) {
      var option = document.createElement("option");
      option.value = item.value;
      option.textContent = item.label;
      select.appendChild(option);
    });
    if (options.some(function (item) { return item.value === oldValue; })) {
      select.value = oldValue;
    }
  }

  function updateControls() {
    var revisions = orderedRevisions();
    var choices = revisions.map(function (revision) {
      return { value: revision.id, label: revisionLabel(revision) };
    });
    var branchChoices = state.branches.map(function (branch) {
      var tip = branchTip(branch.name);
      return { value: branch.name, label: branch.name + " (" + (tip ? tip.id : branch.number) + ")" };
    });
    var targets = [{ value: "trunk", label: "Trunk" }].concat(branchChoices);

    fillSelect(elements.checkoutRevision, choices, "Keine Revisionen");
    fillSelect(elements.diffRevision, choices, "Keine Revisionen");
    fillSelect(elements.branchParent, choices, "Zuerst Revision einchecken");
    fillSelect(elements.checkinTarget, targets, "Trunk");
    elements.checkout.disabled = !revisions.length;
    elements.showDiff.disabled = revisions.length < 2;
    elements.createBranch.disabled = !revisions.length;
    elements.toggleHistory.disabled = !revisions.length;
    elements.download.disabled = !revisions.length;
    elements.revisionCount.textContent = revisions.length + (revisions.length === 1 ? " Revision" : " Revisionen");
    if (selectedRevision && choices.some(function (item) { return item.value === selectedRevision; })) {
      elements.checkoutRevision.value = selectedRevision;
    }
    if (revisions.length > 1 && !elements.diffRevision.value) {
      elements.diffRevision.value = revisions[1].id;
    }
    renderHistory();
    renderHero();
    updateHistoryPreview();
  }

  function renderHistory() {
    var revisions = orderedRevisions();
    if (!revisions.length) {
      elements.revisions.innerHTML = '<div class="empty-history">Dein Verlauf ist noch leer.<br>Checke den ersten Text ein.</div>';
      return;
    }
    elements.revisions.innerHTML = revisions.map(function (revision) {
      var selected = revision.id === selectedRevision ? " selected" : "";
      var date = new Date(revision.createdAt);
      var time = isNaN(date.getTime()) ? "" : date.toLocaleTimeString("de-DE", { hour: "2-digit", minute: "2-digit" });
      return '<div class="revision-row' + selected + '" data-revision="' + escapeHtml(revision.id) + '" tabindex="0" role="button" aria-label="' + escapeHtml(revisionLabel(revision) + ", " + revision.log) + '">' +
        '<div><strong>' + escapeHtml(revision.id) + (revision.branch ? '<em>' + escapeHtml(revision.branch) + '</em>' : "") + '</strong>' +
        '<span>' + escapeHtml(revision.log) + '</span></div><time>' + escapeHtml(time) + '</time></div>';
    }).join("");
    Array.prototype.forEach.call(elements.revisions.querySelectorAll(".revision-row"), function (row) {
      row.addEventListener("click", function () { selectRevision(row.getAttribute("data-revision")); });
      row.addEventListener("keydown", function (event) {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          selectRevision(row.getAttribute("data-revision"));
        }
      });
    });
  }

  function renderHero() {
    var revisions = orderedRevisions().slice(0, 3);
    if (!revisions.length) {
      document.getElementById("hero-head").textContent = "1.3";
      document.getElementById("hero-timeline").innerHTML =
        '<div class="timeline-item"><span class="timeline-node"></span><div><strong>1.3</strong><span>Dokumentation ergänzt</span></div><time>14:32</time></div>' +
        '<div class="timeline-item"><span class="timeline-node"></span><div><strong>1.2</strong><span>Erste Anpassung</span></div><time>14:08</time></div>' +
        '<div class="timeline-item"><span class="timeline-node"></span><div><strong>1.1</strong><span>Erste Revision</span></div><time>13:45</time></div>';
      return;
    }
    var trunk = state.revisions.filter(function (revision) { return !revision.branch; });
    document.getElementById("hero-head").textContent = trunk[trunk.length - 1].id;
    var timeline = document.getElementById("hero-timeline");
    timeline.innerHTML = revisions.map(function (revision) {
      var date = new Date(revision.createdAt);
      var time = isNaN(date.getTime()) ? "" : date.toLocaleTimeString("de-DE", { hour: "2-digit", minute: "2-digit" });
      return '<div class="timeline-item"><span class="timeline-node"></span><div><strong>' +
        escapeHtml(revision.id) + '</strong><span>' + escapeHtml(revision.log) +
        '</span></div><time>' + escapeHtml(time) + '</time></div>';
    }).join("");
  }

  function updateHistoryPreview() {
    elements.historyPreview.textContent = JSON.stringify(state, null, 2);
  }

  function renderResult(content, kind) {
    elements.resultKind.textContent = kind.toUpperCase();
    elements.result.classList.remove("empty-result");
    elements.result.textContent = content;
  }

  function showMessage(message, isError) {
    elements.message.textContent = message;
    elements.message.classList.toggle("error", !!isError);
  }

  function toast(message) {
    elements.toast.textContent = message;
    elements.toast.classList.add("visible");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { elements.toast.classList.remove("visible"); }, 2100);
  }

  function selectRevision(id) {
    var revision = revisionById(id);
    if (!revision) return;
    selectedRevision = id;
    elements.checkoutRevision.value = id;
    elements.content.value = revision.content;
    elements.log.value = "";
    renderHistory();
    renderResult(revision.content, "Checkout " + revision.id);
  }

  function nextTrunkId() {
    var trunk = state.revisions.filter(function (revision) { return !revision.branch; });
    return "1." + (trunk.length + 1);
  }

  function nextBranchId(branch) {
    var revisions = state.revisions.filter(function (revision) { return revision.branch === branch.name; });
    return branch.number + "." + (revisions.length + 1);
  }

  function nextBranchNumber(parentRevision) {
    var prefix = parentRevision + ".";
    var siblings = state.branches.filter(function (branch) {
      return branch.parentRevision === parentRevision;
    });
    var max = siblings.reduce(function (current, branch) {
      var suffix = branch.number.slice(prefix.length);
      var number = /^\d+$/.test(suffix) ? Number(suffix) : 0;
      return Math.max(current, number);
    }, 0);
    return prefix + (max + 1);
  }

  function commit() {
    var content = elements.content.value;
    var log = elements.log.value.trim();
    var target = elements.checkinTarget.value;
    if (content !== "" && !content.endsWith("\n")) {
      return showMessage("Bitte den nicht-leeren Inhalt mit einem Zeilenumbruch beenden.", true);
    }
    if (!log) return showMessage("Bitte einen Änderungskommentar eingeben.", true);

    var parent = null;
    var branchName = null;
    var id;
    if (target === "trunk") {
      var trunk = state.revisions.filter(function (revision) { return !revision.branch; });
      parent = trunk.length ? trunk[trunk.length - 1] : null;
      id = nextTrunkId();
    } else {
      var branch = state.branches.find(function (item) { return item.name === target; });
      if (!branch) return showMessage("Der gewählte Branch existiert nicht mehr.", true);
      parent = branchTip(branch.name);
      branchName = branch.name;
      id = nextBranchId(branch);
    }
    state.revisions.push({
      id: id,
      parentId: parent ? parent.id : null,
      branch: branchName,
      content: content,
      log: log,
      createdAt: new Date().toISOString()
    });
    state.content = content;
    selectedRevision = id;
    var saved = persist();
    updateControls();
    renderResult(content, "Checkout " + id);
    showMessage(saved
      ? "Revision " + id + " wurde lokal eingecheckt."
      : "Revision " + id + " ist nur bis zum Schließen dieser Seite gespeichert.", !saved);
  }

  function createBranch() {
    var name = elements.branchName.value.trim();
    var parentId = elements.branchParent.value;
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,31}$/.test(name)) {
      return showMessage("Branch-Namen: Buchstabe am Anfang, danach Buchstaben, Zahlen, _ oder -.", true);
    }
    if (state.branches.some(function (branch) { return branch.name.toLowerCase() === name.toLowerCase(); }) ||
        state.tags.some(function (tag) { return tag.name.toLowerCase() === name.toLowerCase(); })) {
      return showMessage("Dieser Branch- oder Tag-Name ist bereits vergeben.", true);
    }
    var parent = revisionById(parentId);
    if (!parent) return showMessage("Bitte eine gültige Elternrevision auswählen.", true);
    var branch = {
      name: name,
      number: nextBranchNumber(parent.id),
      parentRevision: parent.id
    };
    state.branches.push(branch);
    state.content = parent.content;
    elements.content.value = parent.content;
    elements.branchName.value = "";
    var saved = persist();
    updateControls();
    elements.checkinTarget.value = name;
    showMessage(saved
      ? "Branch " + name + " wurde aus Revision " + parent.id + " erstellt."
      : "Branch " + name + " ist nur bis zum Schließen dieser Seite gespeichert.", !saved);
  }

  function lines(text) {
    if (text === "") return [];
    return text.replace(/\n$/, "").split("\n");
  }

  function diffLines(before, after) {
    var left = lines(before);
    var right = lines(after);
    if (left.join("\n") === right.join("\n")) return ["  Keine Unterschiede."];
    if (left.length * right.length > 90000) {
      return ["  Diff im Playground auf 90.000 Zeilenpaare begrenzt."];
    }
    var rows = left.length + 1;
    var columns = right.length + 1;
    var table = Array.from({ length: rows }, function () { return new Uint32Array(columns); });
    for (var i = left.length - 1; i >= 0; i--) {
      for (var j = right.length - 1; j >= 0; j--) {
        table[i][j] = left[i] === right[j] ? table[i + 1][j + 1] + 1 :
          Math.max(table[i + 1][j], table[i][j + 1]);
      }
    }
    var result = [];
    var x = 0;
    var y = 0;
    while (x < left.length || y < right.length) {
      if (x < left.length && y < right.length && left[x] === right[y]) {
        result.push("  " + left[x++]);
        y++;
      } else if (y < right.length && (x === left.length || table[x][y + 1] >= table[x + 1][y])) {
        result.push("+ " + right[y++]);
      } else {
        result.push("- " + left[x++]);
      }
    }
    return result;
  }

  function showDiff() {
    var left = revisionById(elements.diffRevision.value);
    var right = revisionById(elements.checkoutRevision.value);
    if (!left || !right) return;
    var output = diffLines(left.content, right.content).map(function (line) {
      var className = line.indexOf("+ ") === 0 ? "diff-add" :
        (line.indexOf("- ") === 0 ? "diff-remove" : "");
      return '<span class="diff-line ' + className + '">' + escapeHtml(line) + '</span>';
    }).join("");
    elements.resultKind.textContent = "DIFF " + left.id + " → " + right.id;
    elements.result.classList.remove("empty-result");
    elements.result.innerHTML = output;
  }

  function downloadHistory() {
    var blob = new Blob([JSON.stringify(state, null, 2)], { type: "application/json" });
    var url = URL.createObjectURL(blob);
    var link = document.createElement("a");
    link.href = url;
    link.download = "sqlitercs-demo-history.json";
    link.click();
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    toast("JSON-Verlauf heruntergeladen.");
  }

  function reset() {
    if (state.revisions.length && !window.confirm("Den lokal gespeicherten Demo-Verlauf wirklich zurücksetzen?")) return;
    state = { revisions: [], branches: [], tags: [], content: "" };
    selectedRevision = "";
    try {
      localStorage.removeItem(storageKey);
    } catch (error) {
      showMessage("Browser-Speicher konnte nicht geleert werden; der Verlauf ist nur lokal zurückgesetzt.", true);
    }
    elements.content.value = "";
    elements.log.value = "";
    elements.result.classList.add("empty-result");
    elements.result.textContent = "Checke deine erste Revision ein, um Inhalt oder Diff zu sehen.";
    elements.resultKind.textContent = "CHECKOUT";
    elements.message.textContent = "";
    updateControls();
  }

  elements.checkin.addEventListener("click", commit);
  elements.createBranch.addEventListener("click", createBranch);
  elements.checkout.addEventListener("click", function () {
    var revision = revisionById(elements.checkoutRevision.value);
    if (!revision) return;
    selectedRevision = revision.id;
    elements.content.value = revision.content;
    renderHistory();
    renderResult(revision.content, "Checkout " + revision.id);
  });
  elements.checkoutRevision.addEventListener("change", function () {
    selectedRevision = elements.checkoutRevision.value;
    renderHistory();
  });
  elements.showDiff.addEventListener("click", showDiff);
  elements.toggleHistory.addEventListener("click", function () {
    showHistory = !showHistory;
    elements.historyPreview.hidden = !showHistory;
    elements.toggleHistory.innerHTML = "Verlaufsdaten ansehen <span aria-hidden=\"true\">" + (showHistory ? "−" : "＋") + "</span>";
  });
  elements.download.addEventListener("click", downloadHistory);
  elements.reset.addEventListener("click", reset);
  elements.content.value = state.content || "";
  updateControls();
  if (state.revisions.length) {
    selectedRevision = state.revisions[state.revisions.length - 1].id;
    selectRevision(selectedRevision);
  }

  Array.prototype.forEach.call(document.querySelectorAll(".copy-button"), function (button) {
    button.addEventListener("click", function () {
      var source = document.querySelector("#" + button.getAttribute("data-copy") + " code");
      if (!navigator.clipboard || !navigator.clipboard.writeText) {
        toast("Kopieren ist in diesem Browser nicht verfügbar.");
        return;
      }
      navigator.clipboard.writeText(source.textContent).then(function () {
        button.textContent = "Kopiert";
        setTimeout(function () { button.textContent = "Kopieren"; }, 1400);
      }).catch(function () {
        toast("Kopieren ist in diesem Browser nicht verfügbar.");
      });
    });
  });
}());
