(function () {
  "use strict";

  var storageKey = "sqlitercs-playground-v1";
  var languageStorageKey = "sqlitercs-language";
  var translations = {
    de: {
      "meta.title": "SQLiteRCS — Revisionsgeschichte in SQLite",
      "meta.description": "SQLiteRCS speichert Textrevisionen als RCS-Archive in SQLite. Lerne den Ablauf kennen und probiere Check-in, Branches und Diff direkt im Browser.",
      "aria.home": "SQLiteRCS Startseite",
      "aria.navigation": "Hauptnavigation",
      "aria.revisionSketch": "Schematische RCS-Revisionsgeschichte",
      "language.switchToEnglish": "Switch language to English",
      "language.switchToGerman": "Sprache auf Deutsch umstellen",
      "nav.flow": "Ablauf",
      "nav.sql": "SQL",
      "nav.install": "Installation",
      "nav.playground": "Playground",
      "hero.eyebrow": "SQLite-Erweiterung · Textuelle RCS-Archive",
      "hero.title": "Revisionsgeschichte.<br><span>Direkt in SQLite.</span>",
      "hero.description": "SQLiteRCS speichert Textrevisionen als RCS-Archive in einer SQLite-Spalte. Lerne den Ablauf kennen — oder probiere Check-in, Branches und Diff direkt hier aus.",
      "hero.demo": "Demo ausprobieren",
      "hero.install": "Installation ansehen",
      "hero.fact1": "RCS-Archiv",
      "hero.fact2": "SQLite Extension",
      "hero.fact3": "Open Source",
      "hero.currentRevision": "Aktuelle Revision",
      "hero.sample3": "Dokumentation ergänzt",
      "hero.sample2": "Erste Anpassung",
      "hero.sample1": "Erste Revision",
      "hero.caption": "Eine Spalte. Jede Revision.",
      "flow.eyebrow": "Ein kleiner, klarer Ablauf",
      "flow.title": "Von Text zu Revision —<br><span>mit SQL.</span>",
      "flow.description": "Die Anwendung steuert, wann eine Revision entsteht. Das Archiv selbst bleibt ein Wert in deiner SQLite-Tabelle.",
      "flow.step1.title": "Archiv ablegen",
      "flow.step1.description": "Eine TEXT-Spalte hält ein RCS-Archiv pro Datensatz.",
      "flow.step2.title": "Änderung einchecken",
      "flow.step2.description": "<code>rcs_checkin</code> liefert ein neues Archiv zurück. Speichere den Rückgabewert.",
      "flow.step3.title": "Version abrufen",
      "flow.step3.description": "Checkout, Metadaten und Diff sind SQL-Funktionen — direkt abfragbar.",
      "sql.eyebrow": "Direkt in SQLite",
      "sql.title": "SQL zum<br><span>Kopieren.</span>",
      "sql.description": "Diese Beispiele laufen mit der installierten Erweiterung. Der Browser-Playground darunter ist davon unabhängig und simuliert den Ablauf.",
      "sql.copyLabel": "SQL-Beispiel kopieren",
      "sql.copy": "Kopieren",
      "sql.copied": "Kopiert",
      "sql.create.title": "Archiv anlegen",
      "sql.create.code": "CREATE TABLE documents (\\n  id INTEGER PRIMARY KEY,\\n  history TEXT NOT NULL DEFAULT ''\\n);\\n\\nINSERT INTO documents (history)\\nVALUES (rcs_checkin(\\n  '',\\n  'Erste Zeile' || char(10),\\n  'Erstellung'\\n));",
      "sql.branch.title": "Branch & Check-in",
      "sql.branch.code": "UPDATE documents\\nSET history = rcs_create_branch(\\n  history, 'feature', '1.1'\\n)\\nWHERE id = 1;\\n\\nUPDATE documents\\nSET history = rcs_branch_checkin(\\n  history, 'feature',\\n  'Erste Zeile' || char(10) || 'Branch' || char(10),\\n  'Feature begonnen'\\n)\\nWHERE id = 1;",
      "sql.read.title": "Revisionen abfragen",
      "sql.read.code": "SELECT rcs_head(history),\\n       rcs_checkout(history, 'feature'),\\n       rcs_diff(history, '1.1', 'feature')\\nFROM documents\\nWHERE id = 1;\\n\\nSELECT rcs_revisions(history)\\nFROM documents\\nWHERE id = 1;",
      "sql.merge.title": "Drei-Wege-Merge",
      "sql.merge.code": "SELECT rcs_merge(\\n  history, '1.1', 'feature', 'stable'\\n)\\nFROM documents\\nWHERE id = 1;",
      "install.eyebrow": "In wenigen Schritten startklar",
      "install.title": "Baue die<br><span>Erweiterung.</span>",
      "install.description": "SQLiteRCS wird als loadable extension gebaut und kann mit SQLite-Anwendungen verwendet werden, die Erweiterungen laden dürfen.",
      "install.source": "Quellcode auf GitHub",
      "install.step1.title": "Voraussetzungen",
      "install.step1.description": "C-Compiler, Python 3 und SQLite-Entwicklungsheader (`sqlite3.h`, `sqlite3ext.h`).",
      "install.step2.title": "Erweiterung bauen",
      "install.step2.code": "# Linux / macOS\\nmake\\n\\n# Windows: x64 Developer Prompt\\nset SQLITE_INCLUDE_DIR=C:\\\\path\\\\to\\\\sqlite\\nbuild.bat",
      "install.step3.title": "Tests ausführen",
      "install.step3.code": "SQLITERCS_EXTENSION=./sqlitercs.so make test\\n\\n# Windows\\nset SQLITERCS_EXTENSION=sqlitercs.dll\\npython -m unittest discover -s tests -v",
      "install.step4.title": "Extension laden",
      "demo.eyebrow": "Dein Browser-Playground",
      "demo.title": "Check-in. Checkout.<br><span>Vergleichen.</span>",
      "demo.description": "Ändere den Text, checke Revisionen ein und erkunde den Verlauf. Probiere auch Branches und verschachtelte Branches.",
      "demo.notice": "<strong>Interaktive Simulation:</strong> Dieser Playground lädt keine SQLite-Erweiterung. Er bildet den Text- und Revisionsablauf im Browser nach. Deine Daten bleiben lokal in diesem Browser.",
      "demo.reset": "Zurücksetzen",
      "demo.workspace": "ARBEITSBEREICH",
      "demo.editDocument": "Dokument bearbeiten",
      "demo.fileContent": "Dateiinhalt",
      "demo.newlineHint": "Nicht-leerer Text muss mit einem Zeilenumbruch enden.",
      "demo.commitMessage": "Änderungskommentar",
      "demo.logPlaceholder": "z. B. Dokumentation ergänzt",
      "demo.branchFrom": "Branch erstellen aus",
      "demo.noRevisionsYet": "Zuerst Revision einchecken",
      "demo.checkinInto": "Einchecken in",
      "demo.trunk": "Trunk",
      "demo.checkin": "Check-in",
      "demo.revisions": "REVISIONEN",
      "demo.local": "LOKAL",
      "demo.viewHistory": "Verlauf ansehen",
      "demo.selectRevision": "Revision auswählen",
      "demo.noRevisions": "Keine Revisionen",
      "demo.checkout": "Checkout",
      "demo.compareWith": "Vergleichen mit",
      "demo.diff": "Diff",
      "demo.result": "ERGEBNIS",
      "demo.emptyResult": "Checke deine erste Revision ein, um Inhalt oder Diff zu sehen.",
      "demo.viewData": "Verlaufsdaten ansehen",
      "demo.hideData": "Verlaufsdaten ausblenden",
      "demo.exportJson": "JSON exportieren",
      "demo.noHistory": "Dein Verlauf ist noch leer.<br>Checke den ersten Text ein.",
      "demo.revisionOne": "{count} Revision",
      "demo.revisionMany": "{count} Revisionen",
      "demo.checkoutLabel": "Checkout {id}",
      "demo.diffKind": "Diff {left} → {right}",
      "demo.diffLabel": "Diff",
      "demo.noDifferences": "  Keine Unterschiede.",
      "demo.diffLimit": "  Diff im Playground auf 90.000 Zeilenpaare begrenzt.",
      "demo.checkoutKind": "CHECKOUT {id}",
      "demo.newlineError": "Bitte den nicht-leeren Inhalt mit einem Zeilenumbruch beenden.",
      "demo.logError": "Bitte einen Änderungskommentar eingeben.",
      "demo.missingBranch": "Der gewählte Branch existiert nicht mehr.",
      "demo.revisionSaved": "Revision {id} wurde lokal eingecheckt.",
      "demo.revisionTemporary": "Revision {id} ist nur bis zum Schließen dieser Seite gespeichert.",
      "demo.branchNameError": "Branch-Namen: Buchstabe am Anfang, danach Buchstaben, Zahlen, _ oder -.",
      "demo.branchDuplicate": "Dieser Branch- oder Tag-Name ist bereits vergeben.",
      "demo.branchParentError": "Bitte eine gültige Elternrevision auswählen.",
      "demo.branchSaved": "Branch {name} wurde aus Revision {id} erstellt.",
      "demo.branchTemporary": "Branch {name} ist nur bis zum Schließen dieser Seite gespeichert.",
      "demo.resetConfirm": "Den lokal gespeicherten Demo-Verlauf wirklich zurücksetzen?",
      "demo.storageResetError": "Browser-Speicher konnte nicht geleert werden; der Verlauf ist nur lokal zurückgesetzt.",
      "demo.exported": "JSON-Verlauf heruntergeladen.",
      "demo.clipboardError": "Kopieren ist in diesem Browser nicht verfügbar.",
      "demo.languageTemporary": "Sprache geändert, aber die Auswahl kann in diesem Browser nicht gespeichert werden.",
      "about.eyebrow": "Gut zu wissen",
      "about.title": "Ein fokussierter<br><span>erster Schritt.</span>",
      "about.description": "SQLiteRCS unterstützt trunk- und verschachtelte Branch-Revisionen, Tags, mehrteilige Diff-Hunks, Drei-Wege-Merges und den Import/Export textueller RCS-Archive.",
      "about.limitations": "Inhalte sind UTF-8-Text, keine Binärdaten; nicht-leere Inhalte enden mit einem Zeilenumbruch. Importierte Zugriffslisten und Sperren werden normalisiert, aber nicht erhalten oder durchgesetzt.",
      "about.api": "Vollständige SQL-API ansehen",
      "footer.tagline": "Revisionsgeschichte als Text. In deiner SQLite-Datenbank."
    },
    en: {}
  };
  translations.en = Object.assign({}, translations.de, {
    "meta.title": "SQLiteRCS — Revision history in SQLite",
    "meta.description": "SQLiteRCS stores text revisions as RCS archives in SQLite. Learn the workflow and try check-in, branches, and diffs in your browser.",
    "aria.home": "SQLiteRCS home",
    "aria.navigation": "Main navigation",
    "aria.revisionSketch": "Illustration of RCS revision history",
    "language.switchToEnglish": "Switch language to English",
    "language.switchToGerman": "Switch language to German",
    "nav.flow": "Workflow",
    "nav.sql": "SQL",
    "nav.install": "Installation",
    "nav.playground": "Playground",
    "hero.eyebrow": "SQLite extension · Text-based RCS archives",
    "hero.title": "Revision history.<br><span>Right in SQLite.</span>",
    "hero.description": "SQLiteRCS stores text revisions as RCS archives in a SQLite column. Learn the workflow—or try check-ins, branches, and diffs right here.",
    "hero.demo": "Try the demo",
    "hero.install": "View installation",
    "hero.fact1": "RCS archive",
    "hero.fact2": "SQLite extension",
    "hero.fact3": "Open source",
    "hero.currentRevision": "Current revision",
    "hero.sample3": "Documentation updated",
    "hero.sample2": "First update",
    "hero.sample1": "First revision",
    "hero.caption": "One column. Every revision.",
    "flow.eyebrow": "A small, clear workflow",
    "flow.title": "From text to revision—<br><span>with SQL.</span>",
    "flow.description": "Your application decides when to create a revision. The archive itself remains a value in your SQLite table.",
    "flow.step1.title": "Store the archive",
    "flow.step1.description": "A TEXT column stores one RCS archive per record.",
    "flow.step2.title": "Check in a change",
    "flow.step2.description": "<code>rcs_checkin</code> returns a new archive. Store the returned value.",
    "flow.step3.title": "Retrieve a version",
    "flow.step3.description": "Checkout, metadata, and diffs are SQL functions—ready to query.",
    "sql.eyebrow": "Right in SQLite",
    "sql.title": "SQL you can<br><span>copy.</span>",
    "sql.description": "These examples run with the installed extension. The browser playground below is independent and only simulates the workflow.",
    "sql.copyLabel": "Copy SQL example",
    "sql.copy": "Copy",
    "sql.copied": "Copied",
    "sql.create.title": "Create an archive",
    "sql.create.code": "CREATE TABLE documents (\\n  id INTEGER PRIMARY KEY,\\n  history TEXT NOT NULL DEFAULT ''\\n);\\n\\nINSERT INTO documents (history)\\nVALUES (rcs_checkin(\\n  '',\\n  'First line' || char(10),\\n  'Created'\\n));",
    "sql.branch.title": "Branch & check in",
    "sql.branch.code": "UPDATE documents\\nSET history = rcs_create_branch(\\n  history, 'feature', '1.1'\\n)\\nWHERE id = 1;\\n\\nUPDATE documents\\nSET history = rcs_branch_checkin(\\n  history, 'feature',\\n  'First line' || char(10) || 'Feature' || char(10),\\n  'Feature started'\\n)\\nWHERE id = 1;",
    "sql.read.title": "Query revisions",
    "sql.read.code": "SELECT rcs_head(history),\\n       rcs_checkout(history, 'feature'),\\n       rcs_diff(history, '1.1', 'feature')\\nFROM documents\\nWHERE id = 1;\\n\\nSELECT rcs_revisions(history)\\nFROM documents\\nWHERE id = 1;",
    "sql.merge.title": "Three-way merge",
    "sql.merge.code": "SELECT rcs_merge(\\n  history, '1.1', 'feature', 'stable'\\n)\\nFROM documents\\nWHERE id = 1;",
    "install.eyebrow": "Up and running in a few steps",
    "install.title": "Build the<br><span>extension.</span>",
    "install.description": "SQLiteRCS builds as a loadable extension and works with SQLite applications that are allowed to load extensions.",
    "install.source": "Source code on GitHub",
    "install.step1.title": "Requirements",
    "install.step1.description": "A C compiler, Python 3, and the SQLite development headers (`sqlite3.h`, `sqlite3ext.h`).",
    "install.step2.title": "Build the extension",
    "install.step2.code": "# Linux / macOS\\nmake\\n\\n# Windows: x64 Developer Prompt\\nset SQLITE_INCLUDE_DIR=C:\\\\path\\\\to\\\\sqlite\\nbuild.bat",
    "install.step3.title": "Run the tests",
    "install.step3.code": "SQLITERCS_EXTENSION=./sqlitercs.so make test\\n\\n# Windows\\nset SQLITERCS_EXTENSION=sqlitercs.dll\\npython -m unittest discover -s tests -v",
    "install.step4.title": "Load the extension",
    "demo.eyebrow": "Your browser playground",
    "demo.title": "Check in. Check out.<br><span>Compare.</span>",
    "demo.description": "Edit the text, check in revisions, and explore the history. Try branches and nested branches too.",
    "demo.notice": "<strong>Interactive simulation:</strong> This playground does not load SQLite. It simulates the text and revision workflow in your browser. Your data stays in this browser.",
    "demo.reset": "Reset",
    "demo.workspace": "WORKSPACE",
    "demo.editDocument": "Edit document",
    "demo.fileContent": "File content",
    "demo.newlineHint": "Non-empty text must end with a newline.",
    "demo.commitMessage": "Change description",
    "demo.logPlaceholder": "e.g. Update documentation",
    "demo.branchFrom": "Create branch from",
    "demo.noRevisionsYet": "Check in a revision first",
    "demo.checkinInto": "Check in to",
    "demo.checkin": "Check in",
    "demo.revisions": "REVISIONS",
    "demo.local": "LOCAL",
    "demo.viewHistory": "Browse history",
    "demo.selectRevision": "Select revision",
    "demo.noRevisions": "No revisions",
    "demo.checkout": "Check out",
    "demo.compareWith": "Compare with",
    "demo.result": "RESULT",
    "demo.emptyResult": "Check in your first revision to see its content or a diff here.",
    "demo.viewData": "View history data",
    "demo.hideData": "Hide history data",
    "demo.exportJson": "Export JSON",
    "demo.noHistory": "Your history is empty.<br>Check in the first text.",
    "demo.revisionOne": "{count} revision",
    "demo.revisionMany": "{count} revisions",
    "demo.checkoutLabel": "Check out {id}",
    "demo.checkoutKind": "CHECKOUT {id}",
    "demo.diffKind": "DIFF {left} → {right}",
    "demo.noDifferences": "  No differences.",
    "demo.diffLimit": "  Playground diff is limited to 90,000 line pairs.",
    "demo.newlineError": "Please end non-empty content with a newline.",
    "demo.logError": "Enter a change description.",
    "demo.missingBranch": "The selected branch no longer exists.",
    "demo.revisionSaved": "Revision {id} checked in and saved in this browser.",
    "demo.revisionTemporary": "Revision {id} is only available until this page is closed.",
    "demo.branchNameError": "Branch names must start with a letter and contain only letters, numbers, _ or -.",
    "demo.branchDuplicate": "A branch or tag already uses this name.",
    "demo.branchParentError": "Select a valid parent revision.",
    "demo.branchSaved": "Branch {name} created from revision {id}.",
    "demo.branchTemporary": "Branch {name} is only available until this page is closed.",
    "demo.resetConfirm": "Reset the locally saved demo history?",
    "demo.storageResetError": "Could not clear browser storage; the history was only reset in memory.",
    "demo.exported": "History downloaded as JSON.",
    "demo.clipboardError": "Copying is not available in this browser.",
    "demo.languageTemporary": "Language changed, but this browser cannot save the preference.",
    "about.eyebrow": "Good to know",
    "about.title": "A focused<br><span>first step.</span>",
    "about.description": "SQLiteRCS supports trunk and nested branch revisions, tags, multi-hunk diffs, three-way merges, and importing and exporting textual RCS archives.",
    "about.limitations": "Content is UTF-8 text, not binary data; non-empty content must end with a newline. Imported access lists and locks are normalized, not preserved or enforced.",
    "about.api": "See the full SQL API",
    "footer.tagline": "Revision history as text. In your SQLite database."
  });
  var language = loadLanguage();
  var state = loadState();
  var selectedRevision = "";
  var showHistory = false;
  var toastTimer;
  var messageState = null;
  var resultState = null;
  var elements = {
    language: document.getElementById("language-toggle"),
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

  function loadLanguage() {
    try {
      return localStorage.getItem(languageStorageKey) === "en" ? "en" : "de";
    } catch (error) {
      return "de";
    }
  }

  function translate(key, values) {
    var text = translations[language][key] || key;
    Object.keys(values || {}).forEach(function (name) {
      text = text.replace(new RegExp("\\{" + name + "\\}", "g"), values[name]);
    });
    return text;
  }

  function applyLanguage(nextLanguage, savePreference) {
    language = nextLanguage;
    document.documentElement.lang = language;
    document.title = translate("meta.title");
    document.querySelector('meta[name="description"]').content =
      translate("meta.description");
    Array.prototype.forEach.call(
      document.querySelectorAll("[data-i18n]"),
      function (element) {
        element.textContent = translate(element.getAttribute("data-i18n"));
      }
    );
    Array.prototype.forEach.call(
      document.querySelectorAll("[data-i18n-html]"),
      function (element) {
        element.innerHTML = translate(element.getAttribute("data-i18n-html"));
      }
    );
    Array.prototype.forEach.call(
      document.querySelectorAll("[data-i18n-aria]"),
      function (element) {
        element.setAttribute("aria-label", translate(element.getAttribute("data-i18n-aria")));
      }
    );
    Array.prototype.forEach.call(
      document.querySelectorAll("[data-i18n-placeholder]"),
      function (element) {
        element.setAttribute("placeholder", translate(element.getAttribute("data-i18n-placeholder")));
      }
    );
    Array.prototype.forEach.call(
      document.querySelectorAll("[data-i18n-content]"),
      function (element) {
        element.setAttribute("content", translate(element.getAttribute("data-i18n-content")));
      }
    );
    elements.language.textContent = language === "de" ? "EN" : "DE";
    elements.language.setAttribute(
      "aria-label",
      translate(language === "de" ? "language.switchToEnglish" : "language.switchToGerman")
    );
    elements.language.setAttribute("aria-pressed", language === "en" ? "true" : "false");
    elements.toggleHistory.querySelector("[data-i18n]").textContent =
      translate(showHistory ? "demo.hideData" : "demo.viewData");
    if (savePreference) {
      try {
        localStorage.setItem(languageStorageKey, language);
      } catch (error) {
        toast("demo.languageTemporary");
      }
    }
    updateControls();
    if (resultState) renderCurrentResult();
    if (messageState) {
      showMessage(messageState.key, messageState.isError, messageState.values);
    }
  }

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
    var targets = [{ value: "trunk", label: translate("demo.trunk") }].concat(branchChoices);

    fillSelect(elements.checkoutRevision, choices, translate("demo.noRevisions"));
    fillSelect(elements.diffRevision, choices, translate("demo.noRevisions"));
    fillSelect(elements.branchParent, choices, translate("demo.noRevisionsYet"));
    fillSelect(elements.checkinTarget, targets, translate("demo.trunk"));
    elements.checkout.disabled = !revisions.length;
    elements.showDiff.disabled = revisions.length < 2;
    elements.createBranch.disabled = !revisions.length;
    elements.toggleHistory.disabled = !revisions.length;
    elements.download.disabled = !revisions.length;
    elements.revisionCount.textContent = translate(
      revisions.length === 1 ? "demo.revisionOne" : "demo.revisionMany",
      { count: revisions.length }
    );
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
      elements.revisions.innerHTML = '<div class="empty-history">' +
        translate("demo.noHistory") + "</div>";
      return;
    }
    elements.revisions.innerHTML = revisions.map(function (revision) {
      var selected = revision.id === selectedRevision ? " selected" : "";
      var date = new Date(revision.createdAt);
      var time = isNaN(date.getTime()) ? "" : date.toLocaleTimeString(
        language === "de" ? "de-DE" : "en-US",
        { hour: "2-digit", minute: "2-digit" }
      );
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
        '<div class="timeline-item"><span class="timeline-node"></span><div><strong>1.3</strong><span>' +
        translate("hero.sample3") + '</span></div><time>14:32</time></div>' +
        '<div class="timeline-item"><span class="timeline-node"></span><div><strong>1.2</strong><span>' +
        translate("hero.sample2") + '</span></div><time>14:08</time></div>' +
        '<div class="timeline-item"><span class="timeline-node"></span><div><strong>1.1</strong><span>' +
        translate("hero.sample1") + '</span></div><time>13:45</time></div>';
      return;
    }
    var trunk = state.revisions.filter(function (revision) { return !revision.branch; });
    document.getElementById("hero-head").textContent = trunk[trunk.length - 1].id;
    var timeline = document.getElementById("hero-timeline");
    timeline.innerHTML = revisions.map(function (revision) {
      var date = new Date(revision.createdAt);
      var time = isNaN(date.getTime()) ? "" : date.toLocaleTimeString(
        language === "de" ? "de-DE" : "en-US",
        { hour: "2-digit", minute: "2-digit" }
      );
      return '<div class="timeline-item"><span class="timeline-node"></span><div><strong>' +
        escapeHtml(revision.id) + '</strong><span>' + escapeHtml(revision.log) +
        '</span></div><time>' + escapeHtml(time) + '</time></div>';
    }).join("");
  }

  function updateHistoryPreview() {
    elements.historyPreview.textContent = JSON.stringify(state, null, 2);
  }

  function renderResult(content, kindKey, values) {
    resultState = { type: "content", content: content, kindKey: kindKey, values: values || {} };
    elements.resultKind.textContent = translate(kindKey, values).toUpperCase();
    elements.result.classList.remove("empty-result");
    elements.result.textContent = content;
  }

  function renderDiff(left, right) {
    var output = diffLines(left.content, right.content).map(function (line) {
      var className = line.indexOf("+ ") === 0 ? "diff-add" :
        (line.indexOf("- ") === 0 ? "diff-remove" : "");
      return '<span class="diff-line ' + className + '">' + escapeHtml(line) + '</span>';
    }).join("");
    elements.resultKind.textContent = translate("demo.diffKind", {
      left: left.id,
      right: right.id
    });
    elements.result.classList.remove("empty-result");
    elements.result.innerHTML = output;
  }

  function renderCurrentResult() {
    if (resultState.type === "diff") {
      var left = revisionById(resultState.leftId);
      var right = revisionById(resultState.rightId);
      if (left && right) renderDiff(left, right);
    } else {
      renderResult(resultState.content, resultState.kindKey, resultState.values);
    }
  }

  function showMessage(key, isError, values) {
    messageState = { key: key, isError: isError, values: values || {} };
    elements.message.textContent = translate(key, values);
    elements.message.classList.toggle("error", !!isError);
  }

  function toast(key) {
    elements.toast.textContent = translate(key);
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
    renderResult(revision.content, "demo.checkoutKind", { id: revision.id });
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
      return showMessage("demo.newlineError", true);
    }
    if (!log) return showMessage("demo.logError", true);

    var parent = null;
    var branchName = null;
    var id;
    if (target === "trunk") {
      var trunk = state.revisions.filter(function (revision) { return !revision.branch; });
      parent = trunk.length ? trunk[trunk.length - 1] : null;
      id = nextTrunkId();
    } else {
      var branch = state.branches.find(function (item) { return item.name === target; });
      if (!branch) return showMessage("demo.missingBranch", true);
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
    renderResult(content, "demo.checkoutKind", { id: id });
    showMessage(saved ? "demo.revisionSaved" : "demo.revisionTemporary", !saved, { id: id });
  }

  function createBranch() {
    var name = elements.branchName.value.trim();
    var parentId = elements.branchParent.value;
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,31}$/.test(name)) {
      return showMessage("demo.branchNameError", true);
    }
    if (state.branches.some(function (branch) { return branch.name.toLowerCase() === name.toLowerCase(); }) ||
        state.tags.some(function (tag) { return tag.name.toLowerCase() === name.toLowerCase(); })) {
      return showMessage("demo.branchDuplicate", true);
    }
    var parent = revisionById(parentId);
    if (!parent) return showMessage("demo.branchParentError", true);
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
    showMessage(saved ? "demo.branchSaved" : "demo.branchTemporary", !saved, {
      name: name,
      id: parent.id
    });
  }

  function lines(text) {
    if (text === "") return [];
    return text.replace(/\n$/, "").split("\n");
  }

  function diffLines(before, after) {
    var left = lines(before);
    var right = lines(after);
    if (left.join("\n") === right.join("\n")) return [translate("demo.noDifferences")];
    if (left.length * right.length > 90000) {
      return [translate("demo.diffLimit")];
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
    resultState = { type: "diff", leftId: left.id, rightId: right.id };
    renderDiff(left, right);
  }

  function downloadHistory() {
    var blob = new Blob([JSON.stringify(state, null, 2)], { type: "application/json" });
    var url = URL.createObjectURL(blob);
    var link = document.createElement("a");
    link.href = url;
    link.download = "sqlitercs-demo-history.json";
    link.click();
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    toast("demo.exported");
  }

  function reset() {
    if (state.revisions.length && !window.confirm(translate("demo.resetConfirm"))) return;
    state = { revisions: [], branches: [], tags: [], content: "" };
    selectedRevision = "";
    resultState = null;
    messageState = null;
    var storageError = false;
    try {
      localStorage.removeItem(storageKey);
    } catch (error) {
      storageError = true;
    }
    elements.content.value = "";
    elements.log.value = "";
    elements.result.classList.add("empty-result");
    elements.result.textContent = translate("demo.emptyResult");
    elements.resultKind.textContent = "CHECKOUT";
    if (storageError) showMessage("demo.storageResetError", true);
    else elements.message.textContent = "";
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
    renderResult(revision.content, "demo.checkoutKind", { id: revision.id });
  });
  elements.checkoutRevision.addEventListener("change", function () {
    selectedRevision = elements.checkoutRevision.value;
    renderHistory();
  });
  elements.showDiff.addEventListener("click", showDiff);
  elements.toggleHistory.addEventListener("click", function () {
    showHistory = !showHistory;
    elements.historyPreview.hidden = !showHistory;
    elements.toggleHistory.querySelector("[data-i18n]").textContent =
      translate(showHistory ? "demo.hideData" : "demo.viewData");
  });
  elements.download.addEventListener("click", downloadHistory);
  elements.reset.addEventListener("click", reset);
  elements.language.addEventListener("click", function () {
    applyLanguage(language === "de" ? "en" : "de", true);
  });
  elements.content.value = state.content || "";
  applyLanguage(language, false);
  updateControls();
  if (state.revisions.length) {
    selectedRevision = state.revisions[state.revisions.length - 1].id;
    selectRevision(selectedRevision);
  }

  Array.prototype.forEach.call(document.querySelectorAll(".copy-button"), function (button) {
    button.addEventListener("click", function () {
      var source = document.querySelector("#" + button.getAttribute("data-copy") + " code");
      if (!navigator.clipboard || !navigator.clipboard.writeText) {
        toast("demo.clipboardError");
        return;
      }
      navigator.clipboard.writeText(source.textContent).then(function () {
        button.textContent = translate("sql.copied");
        setTimeout(function () { button.textContent = translate("sql.copy"); }, 1400);
      }).catch(function () {
        toast("demo.clipboardError");
      });
    });
  });
}());
