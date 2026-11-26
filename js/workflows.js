import {parseFile, mapRows} from "./import-file.js";

const datasets = new Map(Object.entries({
  "accounts/current": "current_accounts", "accounts/savings": "savings_accounts", "accounts/credit": "credit_accounts",
  "history/current": "current_history", "history/savings": "savings_history", expenses: "expenses",
  "planned-expenses": "planned_expenses", "recurring-expenses": "recurring_expenses", income: "income",
  "planned-income": "planned_income", debts: "debts", taxes: "taxes", banks: "banks",
  "expense-categories": "categories", countries: "countries", currencies: "currencies"
}));

const exportFormats = {
  csv: "CSV · UTF-8", ods: "OpenDocument (.ods)", xlsx: "Excel (.xlsx)",
  qif: "QIF", ofx: "OFX", qfx: "QFX"
};

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}

export async function workflow(context) {
  const {root, route, node, tr, table, lookup, options, request,
         defaultCurrency, dateInput, unixTime, statusOptions} = context;
  const projection = route === "reports/balance-projection";
  const distribution = route === "reports/fund-distribution";
  const estimates = route === "reports/estimates";
  const panel = (title) => {
    const form = node("form");
    form.className = "form-panel";
    form.append(node("h2", tr(title)));
    return form;
  };
  const field = (form, key, control) => {
    const label = node("label", tr(key));
    control.name = key;
    label.append(control);
    form.append(label);
    return control;
  };
  const choice = (entries) => {
    const select = node("select");
    for (const [id, label] of entries) select.add(new Option(label, id));
    return select;
  };
  const input = (type) => Object.assign(node("input"), {type});
  const action = (form, title, callback) => {
    const button = node("button", tr(title));
    const error = node("p");
    error.className = "error";
    error.setAttribute("role", "alert");
    form.append(button, error);
    form.onsubmit = async event => {
      event.preventDefault();
      error.textContent = "";
      button.disabled = true;
      try { await callback(); }
      catch (e) {
        const key = "errors." + e.message;
        const message = tr(key);
        error.textContent = message === key ? e.message : message;
      }
      finally { button.disabled = false; }
    };
  };
  const back = (form, fn) => {
    const button = node("button", tr("back"));
    button.type = "button";
    button.className = "quiet";
    button.onclick = fn;
    form.append(button);
  };
  const selectRef = async (form, key, resource, required) => {
    const control = options(await lookup(resource), tr(required ? "choose_value" : "all_values"));
    control.required = required;
    if (key === "id_currency" && defaultCurrency != null && !estimates && !projection && !distribution) {
      control.value = defaultCurrency;
      if (control.selectedIndex < 0) control.selectedIndex = 0;
    }
    return field(form, key, control);
  };
  const filterFields = async (form, dataset) => {
    const kind = dataset.split("/")[1] || "current";
    const reference = ["banks", "countries", "currencies", "expense-categories"].includes(dataset);
    const controls = {};
    if (!reference) {
      controls.id_account = await selectRef(form, "id_account", "accounts/" + kind, false);
      controls.id_currency = await selectRef(form, "id_currency", "currencies", false);
    }
    if (!reference && !dataset.startsWith("accounts/")) {
      controls.from = field(form, "from_date", dateInput());
      controls.to = field(form, "to_date", dateInput());
    }
    if (["planned-income", "planned-expenses"].includes(dataset))
      controls.status = field(form, "status", statusOptions(tr("all_values")));
    return () => {
      const filters = {};
      for (const [key, control] of Object.entries(controls)) {
        if (!control.value) continue;
        if (key === "from" || key === "to")
          filters[key] = unixTime(control, key === "to");
        else filters[key] = Number(control.value);
      }
      return filters;
    };
  };

  if (route === "data/import") {
    const fileForm = panel("select_file");
    const file = field(fileForm, "source_file", input("file"));
    file.accept = ".csv,.qif,.ofx,.qfx";
    file.required = true;
    const fileFormat = field(fileForm, "file_format", choice(["csv","qif","ofx","qfx"].map(x => [x, x.toUpperCase()])));
    file.onchange = () => {
      const ext = file.files[0]?.name.split(".").pop().toLowerCase();
      if (["csv","qif","ofx","qfx"].includes(ext)) fileFormat.value = ext;
      fileFormat.dispatchEvent(new Event("change"));
    };
    const encoding = field(fileForm, "encoding", choice(["UTF-8", "windows-1252", "windows-1251", "UTF-16LE", "UTF-16BE"].map(x => [x, x])));
    const delimiter = field(fileForm, "delimiter", choice([[",", tr("comma")], [";", tr("semicolon")], ["\t", tr("tab")]]));
    const header = field(fileForm, "has_header", input("checkbox"));
    header.checked = true;
    header.parentElement.className = "check";
    fileFormat.onchange = () => {
      delimiter.parentElement.hidden = header.parentElement.hidden = fileFormat.value !== "csv";
    };
    action(fileForm, "map_file", async () => {
      const source = file.files[0];
      if (source.size > 512 * 1024) throw new Error("import_limit");
      let text;
      try { text = new TextDecoder(encoding.value, {fatal: true}).decode(await source.arrayBuffer()); }
      catch { throw new Error("invalid_encoding"); }
      const type = fileFormat.value;
      const parsed = parseFile(text, type, {delimiter: delimiter.value, header: header.checked});
      const mappingForm = panel("map_file");
      mappingForm.append(node("p", source.name + " · " + parsed.records.length + " " + tr("records")));
      field(mappingForm, "destination", choice([["expenses", tr("expenses")]]));
      if (parsed.records[0].source_account)
        mappingForm.append(node("p", tr("source_account") + " · " + parsed.records[0].source_account));
      const account = await selectRef(mappingForm, "id_account", "accounts/current", true);
      const currency = await selectRef(mappingForm, "id_currency", "currencies", true);
      const category = await selectRef(mappingForm, "id_category", "expense-categories", true);
      const mapped = {};
      const csv = type === "csv";
      if (csv) {
        const columns = parsed.columns.map((title, i) => [String(i), (i + 1) + " · " + title]);
        for (const key of ["name","amount","date","description","currency","source_id"]) {
          const picker = options(columns);
          picker.required = ["name","amount","date"].includes(key);
          const index = parsed.columns.findIndex(x => x.trim().toLowerCase() === key);
          if (index >= 0) picker.value = String(index);
          mapped[key] = field(mappingForm, key, picker);
        }
        const example = Object.fromEntries(parsed.records[0].map((v, i) => [columns[i]?.[1] ?? String(i + 1), v]));
        mappingForm.append(table([example], Object.keys(example), ""));
      }
      const dates = type === "ofx" || type === "qfx" ? [["ofx", "OFX"]] :
        (type === "qif" ? ["mdy","dmy","iso"] : ["iso","mdy","dmy","unix"]).map(x => [x, tr("date_" + x)]);
      const date = field(mappingForm, "date_format", choice(dates));
      const sign = field(mappingForm, "sign", choice([["negative",tr("negative_debits")],["positive",tr("positive_expenses")]]));
      sign.value = csv ? "positive" : "negative";
      const ofx = type === "ofx" || type === "qfx";
      sign.disabled = ofx;
      const decimal = field(mappingForm, "decimal", choice([[".","."],[",",","]]));
      decimal.disabled = ofx;
      back(mappingForm, () => root.replaceChildren(fileForm));
      action(mappingForm, "review", async () => {
        const mapping = Object.fromEntries(Object.entries(mapped).map(([key, element]) => [key, element.value]));
        const rows = mapRows(parsed, {mapping, sign: sign.value, date: date.value, decimal: decimal.value});
        const preview = await request("import/preview", "POST", {dataset: "expenses", rows, id_account: +account.value,
          id_category: +category.value, id_currency: +currency.value});
        const review = panel("review");
        review.classList.add("review");
        review.append(node("p", [account, currency, category].map(s => s.selectedOptions[0].text).join(" · ")));
        const details = preview.rows.map((row, i) => ({row: i + 1, name: row.name, date: row.date, amount: row.amount, currency: row.currency || currency.selectedOptions[0].text,
          source_id: row.source_id, result: row.error ? tr("errors." + row.error) : tr(row.duplicate ? "duplicate" : "ready")}));
        const grid = table(details, ["row","name","date","amount","currency","source_id","result"], "expenses");
        const heading = node("th", tr("include"));
        heading.scope = "col";
        grid.querySelector("thead tr").prepend(heading);
        const checks = preview.rows.map((row, i) => {
          const check = input("checkbox");
          check.setAttribute("aria-label", tr("include") + " " + (i + 1));
          check.disabled = Boolean(row.error);
          check.checked = !row.error && !row.duplicate;
          const cell = node("td");
          cell.append(check);
          grid.querySelector("tbody").rows[i].prepend(cell);
          return check;
        });
        review.append(grid);
        const detailButton = node("button", tr("download_review"));
        detailButton.type = "button";
        detailButton.className = "quiet";
        detailButton.onclick = () => downloadBlob(new Blob([JSON.stringify(details, null, 2)], {type: "application/json"}), "pvledger-import-review.json");
        review.append(detailButton);
        back(review, () => root.replaceChildren(mappingForm));
        action(review, "confirm_import", async () => {
          const chosen = checks.flatMap((check, i) => check.checked ? [i] : []);
          if (!chosen.length) throw new Error("choose_rows");
          const result = await request("import/commit", "POST", {token: preview.token, confirm: true, rows: chosen});
          const summary = node("section");
          summary.append(node("h2", tr("import_complete")), table([result], ["imported","skipped","failed"], ""));
          detailButton.onclick = () => downloadBlob(new Blob([JSON.stringify(details.map((item, i) => ({
            ...item, outcome: tr(chosen.includes(i) ? "imported" : "skipped")
          })), null, 2)], {type: "application/json"}), "pvledger-import-results.json");
          summary.append(detailButton);
          root.replaceChildren(summary);
        });
        root.replaceChildren(review);
      });
      root.replaceChildren(mappingForm);
    });
    root.append(fileForm);
    return;
  }

  if (route === "data/export") {
    const scope = panel("export_scope");
    const fileFormat = field(scope, "file_format", choice(Object.entries(exportFormats)));
    const dataset = field(scope, "dataset", choice(Array.from(datasets, ([id, key]) => [id, tr(key)])));
    const compatibility = node("p", tr("transaction_export_hint"));
    compatibility.hidden = true;
    scope.append(compatibility);
    const tabular = () => ["csv", "ods", "xlsx"].includes(fileFormat.value);
    fileFormat.onchange = () => {
      compatibility.hidden = tabular();
      for (const option of dataset.options)
        option.hidden = option.disabled = !tabular() && !["expenses", "income"].includes(option.value);
      if (dataset.selectedOptions[0]?.disabled) dataset.value = "expenses";
    };
    action(scope, "next", async () => {
      const source = dataset.value;
      const format = fileFormat.value;
      const selectColumns = tabular();
      const form = panel(selectColumns ? "export_fields" : "export_filters");
      const filters = await filterFields(form, source);
      const checks = [];
      if (selectColumns) {
        const initial = await request("export/preview", "POST", {dataset: source, format});
        const fields = node("fieldset");
        fields.append(node("legend", tr("columns")));
        for (const key of initial.columns) {
          const check = input("checkbox");
          check.checked = true;
          const label = node("label", key.endsWith("_label") ? tr(key.slice(0,-6)) + " · " + tr("label") : tr(key));
          label.className = "check";
          label.prepend(check);
          fields.append(label);
          checks.push([key, check]);
        }
        form.append(fields);
      }
      back(form, () => root.replaceChildren(scope));
      action(form, "review", async () => {
        const columns = checks.filter(([,check]) => check.checked).map(([key]) => key);
        const payload = {dataset: source, format, filters: filters()};
        if (selectColumns) {
          if (!columns.length) throw new Error("choose_columns");
          payload.columns = columns;
        }
        const preview = await request("export/preview", "POST", payload);
        const review = panel("review");
        review.classList.add("review");
        review.append(node("p", tr(datasets.get(source)) + " · " + preview.total + " " + tr("records")),
                      table(preview.items, payload.columns || preview.columns, source));
        back(review, () => root.replaceChildren(form));
        if (preview.total > preview.limit) review.append(node("p", tr("errors.narrow_filters")));
        else action(review, "download", async () => {
          const file = await request("export/download", "POST", payload);
          downloadBlob(file, "pvledger-" + source.replace("/", "-") + "." + format);
        });
        root.replaceChildren(review);
      });
      root.replaceChildren(form);
    });
    root.append(scope);
    return;
  }

  const balances = route === "reports/balances";
  const breakdown = route === "reports/expense-categories";
  const filters = panel("report_filters");
  if (estimates) filters.append(node("p", tr("estimates_hint")));
  if (projection || distribution) filters.append(node("p", tr(projection ? "projection_hint" : "distribution_hint")));
  const result = node("div");
  const kind = balances ? field(filters, "account_type", choice([["current",tr("current_accounts")],["savings",tr("savings_accounts")]])) : null;
  const controls = node("div");
  filters.append(controls);
  let read;
  async function configure() {
    const next = node("div");
    const selected = kind?.value;
    let nextRead;
    if (projection || distribution) {
      const currency = await selectRef(next, "id_currency", "currencies", false);
      const to = projection ? field(next, "to_date", dateInput()) : null;
      nextRead = () => {
        const query = {};
        if (currency.value) query.id_currency = currency.value;
        if (to?.value) query.to = unixTime(to, true);
        return query;
      };
    } else nextRead = await filterFields(next, balances ? "history/" + selected : "expenses");
    if (kind && selected !== kind.value) return;
    read = nextRead;
    controls.replaceChildren(next);
    await display();
  }
  let pending = 0;
  async function display() {
    const serial = ++pending;
    const params = new URLSearchParams(read());
    if (balances) params.set("account_type", kind.value);
    const {items} = await request(route + "?" + params);
    if (serial !== pending) return;
    const sections = new Map();
    for (const row of items) {
      const key = balances ? row.id_currency + "/" + row.id_account : row.id_currency;
      if (!sections.has(key)) sections.set(key, []);
      sections.get(key).push(row);
    }
    result.replaceChildren();
    if (!items.length) result.append(node("p", tr("empty")));
    for (const rows of sections.values()) {
      const section = node("section"), currency = rows[0].currency;
      const title = balances ? rows[0].account + " · " + currency : currency;
      section.append(node("h2", title));
      result.append(section);
      if (projection) {
        const series = new Map();
        for (const row of rows) {
          const key = row.account_type + "/" + row.id_account;
          if (!series.has(key)) series.set(key, []);
          series.get(key).push(row);
        }
        const end = Math.max(...rows.map(row => row.day), params.has("to") ? Number(params.get("to")) - 86399 : 0);
        for (const points of series.values()) {
          const first = points[0];
          const total = first.id_account === null;
          const label = total ? tr("total_balance") : first.account + " · " + tr(first.account_type + "_accounts");
          const wrap = node(total ? "section" : "details");
          wrap.className = "report-series";
          wrap.append(node(total ? "h3" : "summary", label));
          const chart = node("canvas");
          chart.className = "chart";
          chart.setAttribute("role", "img");
          chart.setAttribute("aria-label", tr("balance_projection") + " · " + currency + " · " + label);
          wrap.append(chart, table(points, ["day", "delta", "amount"], route));
          timeline(chart, points, context, "day", end);
          section.append(wrap);
        }
        continue;
      }
      if (distribution) {
        const displayRows = rows.map(row => ({...row, account_type: tr(row.account_type + "_accounts")}));
        const grid = table(displayRows, ["account", "account_type", "amount"], route);
        distributionChart(grid, rows, context);
        section.append(grid);
        continue;
      }
      if (estimates) {
        const displayRows = rows.map(row => ({...row, basis: tr(row.basis),
          estimate_income: row.income, estimate_expenses: row.expenses}));
        section.append(table(displayRows, ["day", "basis", "estimate_income", "estimate_expenses", "net"], route));
        continue;
      }
      const chart = node("canvas");
      chart.className = "chart";
      chart.setAttribute("role", "img");
      chart.setAttribute("aria-label", document.querySelector("main h1").textContent + " · " + title);
      if (!breakdown) {
        const timeKey = balances ? "date" : "day";
        section.append(chart);
        timeline(chart, rows, context, timeKey);
        section.append(table(rows, balances ? ["id", "date", "amount"] : ["day", "amount"], route));
        continue;
      }
      const total = rows.reduce((sum, row) => sum + row.amount, 0);
      const colors = rows.map((_, i) => 'hsl(' + ((165 + i * 137.5) % 360) + ' 54% 38%)');
      const pie = total > 0 && rows.every(row => row.amount >= 0);
      if (pie) {
        section.append(chart);
        pieChart(chart, rows, total, colors);
      } else section.append(node("p", tr("pie_unavailable")));
      if (pie) for (const row of rows) row.share = new Intl.NumberFormat(context.locale,
        {style:"percent",maximumFractionDigits:2}).format(row.amount / total);
      const grid = table(rows, pie ? ["category","amount","share"] : ["category","amount"], route);
      if (pie) Array.from(grid.querySelector("tbody").rows).forEach((row, i) => {
        const swatch = node("canvas");
        swatch.width = swatch.height = 12;
        swatch.className = "chart-key";
        const brush = swatch.getContext("2d");
        brush.fillStyle = colors[i];
        brush.fillRect(0, 0, 12, 12);
        row.cells[0].prepend(swatch);
      });
      section.append(grid);
    }
  }
  action(filters, "filter", display);
  if (kind) kind.onchange = () => configure().catch(error => result.replaceChildren(node("p", error.message)));
  root.append(filters, result);
  await configure();
}

function pieChart(canvas, rows, total, colors) {
  canvas.width = canvas.height = 240;
  canvas.classList.add("pie");
  const ctx = canvas.getContext("2d");
  let angle = -Math.PI / 2;
  rows.forEach((row, i) => {
    const end = angle + 2 * Math.PI * row.amount / total;
    ctx.beginPath();
    ctx.moveTo(120,120);
    ctx.arc(120,120,104,angle,end);
    ctx.closePath();
    ctx.fillStyle = colors[i];
    ctx.fill();
    angle = end;
  });
}

function distributionChart(grid, data, {node, tr}) {
  const rows = grid.querySelector("table");
  const heading = node("th", tr("balance_bar"));
  heading.scope = "col";
  rows.tHead.rows[0].append(heading);
  const min = Math.min(0, ...data.map(row => row.amount));
  const max = Math.max(0, ...data.map(row => row.amount));
  const scale = Math.max(-min, max) || 1;
  const span = max / scale - min / scale;
  const position = value => span ? 2 + (value / scale - min / scale) / span * 236 : 2;
  const zero = position(0);
  const colors = getComputedStyle(grid);
  Array.from(rows.tBodies[0].rows).forEach((row, i) => {
    const canvas = node("canvas");
    canvas.width = 240;
    canvas.height = 18;
    canvas.className = "balance-bar";
    canvas.setAttribute("aria-hidden", "true");
    row.insertCell().append(canvas);
    const brush = canvas.getContext("2d");
    brush.fillStyle = colors.getPropertyValue(data[i].amount < 0 ? "--danger" : "--accent").trim();
    const value = position(data[i].amount);
    brush.fillRect(Math.min(zero, value), 3, Math.abs(value - zero), 12);
    brush.fillStyle = colors.color;
    brush.fillRect(zero, 0, 1, 18);
  });
}

function timeline(canvas, rows, {format, locale}, timeKey, through) {
  canvas.width = 720;
  canvas.height = 250;
  const ctx = canvas.getContext("2d");
  const colors = getComputedStyle(canvas);
  ctx.font = "14px sans-serif";
  const min = Math.min(...rows.map(r => r.amount)), max = Math.max(...rows.map(r => r.amount));
  const start = rows[0][timeKey], last = rows[rows.length - 1];
  const end = through ?? last[timeKey];
  const bounds = [min,max].map(v => new Intl.NumberFormat(locale, {maximumSignificantDigits: 6}).format(v));
  const left = Math.max(...bounds.map(v => ctx.measureText(v).width)) + 12;
  const x = t => start === end ? (left + 705) / 2 : left + (t - start) / (end - start) * (705 - left);
  const y = v => max === min ? 110 : 200 - (v - min) / (max - min) * 180;
  ctx.strokeStyle = "#a6b4b0";
  ctx.beginPath(); ctx.moveTo(left, 14); ctx.lineTo(left, 205); ctx.lineTo(705,205); ctx.stroke();
  ctx.fillStyle = colors.color;
  ctx.fillText(bounds[0], 1, y(min) + 4);
  if (max !== min) ctx.fillText(bounds[1], 1, y(max) + 4);
  ctx.fillText(format(timeKey, start, ""), left, 230);
  if (start !== end) { ctx.textAlign = "right"; ctx.fillText(format(timeKey, end, ""), 710, 230); }
  ctx.strokeStyle = ctx.fillStyle = colors.getPropertyValue("--accent").trim();
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(x(start), y(rows[0].amount));
  for (let i = 1; i < rows.length; ++i) {
    const r = rows[i];
    if (through !== undefined) ctx.lineTo(x(r[timeKey]), y(rows[i - 1].amount));
    ctx.lineTo(x(r[timeKey]), y(r.amount));
  }
  if (through !== undefined) ctx.lineTo(x(end), y(last.amount));
  ctx.stroke();
  for (const r of rows) {
    ctx.beginPath(); ctx.arc(x(r[timeKey]), y(r.amount), 3, 0, Math.PI * 2); ctx.fill();
  }
}
