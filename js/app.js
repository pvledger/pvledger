"use strict";

document.addEventListener("DOMContentLoaded", async () => {
  const root = document.querySelector("#workspace");
  const login = document.querySelector("#login-form");
  const settings = document.querySelector("#settings-form");
  const restore = document.querySelector("#restore-form");
  if (!root && !login && !settings && !restore) return;

  const csrf = document.querySelector('meta[name="csrf-token"]').content;
  const locale = document.documentElement.lang;
  let labels = {};
  const request = async (route, method = "GET", payload, token = csrf) => {
    const headers = {"X-CSRF-Token": token};
    if (payload !== undefined) headers["Content-Type"] = "application/json";
    const response = await fetch("/api/v1/" + route, {
      method, headers, body: payload === undefined ? undefined : JSON.stringify(payload)
    });
    if (response.status === 401 && !login) location.assign("/login");
    if (response.status === 204) return null;
    if (response.ok && response.headers.get("Content-Disposition")?.startsWith("attachment")) return response.blob();
    if (!response.headers.get("Content-Type")?.includes("application/json")) throw new Error(await response.text());
    const body = await response.json();
    if (!response.ok) throw new Error(body.error?.message || labels["errors.internal"]);
    return body;
  };
  const submitted = (form, handler) => form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const message = form.querySelector("[data-error]");
    const button = form.querySelector('button[type="submit"]');
    message.textContent = "";
    button.disabled = true;
    try { await handler(Object.fromEntries(new FormData(form))); }
    catch (error) { message.textContent = error.message; }
    finally { button.disabled = false; }
  });
  if (restore) {
    submitted(restore, async ({backup}) => {
      if (restore.dataset.confirm && !window.confirm(restore.dataset.confirm)) return;
      const response = await fetch(restore.action, {
        method: "POST", body: backup,
        headers: {"Content-Type": "application/octet-stream", "X-CSRF-Token": restore.dataset.token}
      });
      if (!response.ok) {
        if (response.headers.get("Content-Type")?.includes("application/json"))
          throw new Error((await response.json()).error.message);
        throw new Error(await response.text());
      }
      location.assign("/login");
    });
    if (!settings) return;
  }
  if (login) {
    submitted(login, async (form) => {
      await request("auth/login", "POST", form, "login");
      location.assign("/");
    });
    return;
  }
  if (settings) {
    submitted(settings, async (form) => {
      await request("settings", "PATCH", form);
      location.reload();
    });
    return;
  }

  const node = (tag, text) => {
    const element = document.createElement(tag);
    if (text !== undefined) element.textContent = text;
    return element;
  };
  const tr = (name) => labels[name] || labels["fields." + name] || name;
  const link = (href, text) => {
    const anchor = node("a", text);
    anchor.href = href;
    return anchor;
  };
  const lookupCache = new Map();
  const lookup = (resource) => {
    if (!lookupCache.has(resource)) lookupCache.set(resource, (async () => {
      const values = new Map();
      let page = 1, rows;
      do {
        rows = await request("lookups/" + resource + "?page=" + page++);
        for (const {id, label} of rows) values.set(String(id), label);
      } while (rows.length === 100);
      return values;
    })());
    return lookupCache.get(resource);
  };
  const options = (entries, prompt = "—") => {
    const select = node("select");
    select.add(new Option(prompt, ""));
    for (const [id, label] of entries) select.add(new Option(label, id));
    return select;
  };
  const statusOptions = (prompt) => options([0, 1, 2, 3].map(code => [code, tr("statuses." + code)]), prompt);
  const dateField = key => ["date", "billing_date", "from", "to"].includes(key);
  const calendarDay = (value) => {
    const seconds = Number(value);
    return value != null && value !== "" && Number.isInteger(seconds) &&
      seconds >= -62135596800 && seconds <= 253402300799
      ? new Date(seconds * 1000).toISOString().slice(0, 10) : "";
  };
  const dateInput = () => Object.assign(node("input"), {type: "date", min: "0001-01-01", max: "9999-12-31"});
  const unixTime = (input, end = false) => input.valueAsNumber / 1000 + (end ? 86399 : 0);
  const referenceLabels = new Map();
  const dayFormat = new Intl.DateTimeFormat(locale, {dateStyle: "medium", timeZone: "UTC"});
  const timestampFormat = new Intl.DateTimeFormat(locale, {dateStyle: "medium", timeStyle: "medium", timeZone: "UTC"});
  const format = (key, value, route) => {
    if (value === null || value === undefined) return "—";
    if (referenceLabels.has(key)) return referenceLabels.get(key).get(String(value)) ?? "—";
    if ((key === "day" || key === "date" || key === "billing_date") && Number.isSafeInteger(value) &&
        Math.abs(value) <= 8640000000000) {
      const date = new Date(value * 1000);
      return key === "day" ? dayFormat.format(date) : timestampFormat.format(date) + " UTC";
    }
    if (key === "status" && Number.isInteger(value) && value >= 0 && value <= 3)
      return tr("statuses." + value);
    if (typeof value === "number" && !key.startsWith("id") && key !== "status")
      return new Intl.NumberFormat(locale, {maximumFractionDigits: 10}).format(value);
    return String(value);
  };
  const table = (rows, columns, route, detail = false) => {
    if (!rows.length) return node("p", tr("empty"));
    const wrapper = node("div");
    wrapper.className = "table-wrap";
    const grid = node("table");
    const head = grid.createTHead().insertRow();
    for (const key of columns) {
      const label = key === "amount" && route === "accounts/credit" ? "remaining_debt"
        : key === "amount" && route.endsWith("/history") ? "snapshot_balance" : key;
      const cell = node("th", tr(label));
      cell.scope = "col";
      head.append(cell);
    }
    const body = grid.createTBody();
    for (const record of rows) {
      const row = body.insertRow();
      for (const key of columns) {
        const cell = row.insertCell();
        const value = format(key, record[key], route);
        if (key === "id" && detail) cell.append(link("/" + route + "/" + record.id, value));
        else cell.textContent = value;
      }
    }
    wrapper.append(grid);
    return wrapper;
  };
  const route = root.dataset.route;
  const query = new URLSearchParams(location.search);
  const pageLinks = (output) => {
    if (output.total <= output.page_size) return;
    const panel = node("div");
    panel.className = "pagination";
    const jump = (page, label) => {
      const params = new URLSearchParams(query);
      params.set("page", page);
      return link("?" + params, tr(label));
    };
    if (output.page > 1) panel.append(jump(output.page - 1, "previous"));
    if (output.page * output.page_size < output.total) panel.append(jump(output.page + 1, "next"));
    root.append(panel);
  };
  const filters = (keys) => {
    const form = node("form");
    form.className = "filters";
    form.method = "get";
    for (const key of keys) {
      const label = node("label", tr(dateField(key) ? key + "_date" : key));
      let input;
      if (dateField(key)) input = dateInput();
      else if (key === "status") input = statusOptions(tr("all_values"));
      else input = options(referenceLabels.get(key));
      input.name = key;
      input.value = dateField(key) ? calendarDay(query.get(key)) : query.get(key) || "";
      label.append(input);
      form.append(label);
    }
    const button = node("button", tr("filter"));
    button.type = "submit";
    form.append(button);
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      const params = new URLSearchParams();
      for (const [key, value] of new FormData(form)) {
        if (value) params.set(key, dateField(key) ? unixTime(form.elements.namedItem(key), key === "to") : value);
      }
      location.search = params.toString();
    });
    return form;
  };
  const editor = (base, fields, row, actor) => {
    const form = node("form");
    form.className = "form-panel";
    const id = row?.id;
    const tracksBalance = base === "accounts/current" || base === "accounts/savings";
    if (base === "expenses" || base === "income")
      form.append(node("p", tr(id ? "edit_posted_entry" : "post_" + base)));
    const values = new Map();
    for (const field of fields) {
      const name = field.name;
      if (name === "id" || name === "is_admin" || (name === "id_user" && actor.id_role !== 1)) continue;
      const label = node("label", tr(name));
      let input;
      if (field.resource) input = options(referenceLabels.get(name));
      else if (dateField(name)) input = dateInput();
      else if (name === "status") input = statusOptions();
      else input = node("input");
      input.name = name;
      input.required = Boolean(field.notnull || name === "username" || (!id && name === "password"));
      if (name === "amount" && ((!id && base === "expenses") || (tracksBalance && row?.amount != null))) input.required = true;
      if (name === "password") {
        input.type = "password";
        input.autocomplete = "new-password";
        input.minLength = 12;
      } else if (input.tagName === "INPUT" && !dateField(name) && field.type !== "TEXT") {
        input.type = "number";
        input.step = field.type === "REAL" ? "any" : "1";
        if (name.startsWith("id_")) input.min = "1";
      }
      const current = row?.[name];
      if (dateField(name)) input.value = calendarDay(current);
      else if (name === "status" && current != null && (current < 0 || current > 3)) {
        const legacy = new Option(tr("legacy_status") + " (" + current + ")", String(current));
        legacy.disabled = true;
        input.add(legacy);
        input.value = legacy.value;
      } else if (name !== "password" && current != null) input.value = current;
      else if (!row && name === "id_currency" && actor.default_currency != null) input.value = actor.default_currency;
      values.set(name, {field, input, initial: input.value});
      label.append(input);
      form.append(label);
    }
    const save = node("button", tr(id ? "save" : "add"));
    save.type = "submit";
    const error = node("p");
    error.dataset.error = "";
    error.className = "error";
    error.setAttribute("role", "alert");
    form.append(save, error);
    submitted(form, async () => {
      const payload = {};
      for (const [name, {field, input, initial}] of values) {
        if (name === "password" && !input.value && id) continue;
        const value = input.value;
        if (id && value === initial && (dateField(name) || name === "status" || (tracksBalance && name === "amount"))) continue;
        if (value === "") payload[name] = null;
        else if (dateField(name)) payload[name] = unixTime(input);
        else if (name === "status") payload[name] = Number(value);
        else if (field.type === "TEXT" || field.type === "INTEGER") payload[name] = value;
        else payload[name] = Number(value);
      }
      const saved = await request(id ? base + "/" + id : base, id ? "PATCH" : "POST", payload);
      location.assign("/" + base + "/" + saved.id);
    });
    return form;
  };

  try {
    const [translations, actor] = await Promise.all([
      request("i18n/" + locale.replace("-", "_")), request("auth/me")
    ]);
    labels = translations;
    if (route.startsWith("data/") || route.startsWith("reports/")) {
      const {workflow} = await import("/assets/js/workflows.js");
      await workflow({root, route, node, tr, table, lookup, options, format, request, locale,
                      defaultCurrency: actor.default_currency, dateInput, unixTime, statusOptions});
      return;
    }
    const queryString = query.size ? "?" + query : "";
    if (route === "dashboard") {
      const output = await request(route);
      const cards = node("div");
      cards.className = "dashboard-grid";
      for (const key of ["current_accounts", "savings_accounts", "credit_accounts", "debts", "recent_expenses"]) {
        const section = node("section");
        section.append(node("h2", tr(key)));
        section.append(table(output[key], key === "recent_expenses" ?
          ["name", "amount", "currency", "date"] : ["currency", "name", "amount"], route));
        cards.append(section);
      }
      root.append(cards);
      return;
    }
    if (route === "plugins") {
      for (const plugin of await request(route)) {
        const card = node("section");
        card.append(node("h2", plugin.name));
        const error = node("p");
        error.setAttribute("role", "alert");
        const action = node("button", tr(plugin.enabled === 1 ? "disable" : "enable"));
        action.disabled = !plugin.available;
        action.addEventListener("click", async () => {
          action.disabled = true;
          try {
            await request("plugins/" + plugin.id + "/" + (plugin.enabled === 1 ? "disable" : "enable"), "POST");
            location.reload();
          } catch (failure) { error.textContent = failure.message; action.disabled = false; }
        });
        if (!plugin.available) card.append(node("p", tr("unavailable")));
        card.append(action, error);
        root.append(card);
      }
      if (!root.childElementCount) root.append(node("p", tr("empty")));
      return;
    }
    if (route.endsWith("/history")) {
      const output = await request(route + queryString);
      root.append(link("/" + route.slice(0, -8), tr("back")));
      root.append(filters(["from", "to"]));
      root.append(node("p", tr("currency") + " · " + (output.currency.iso || output.currency.name)));
      root.append(table(output.items, ["date", "amount"], route));
      pageLinks(output);
      return;
    }
    const detail = /\/\d+$/.test(route);
    const base = detail ? route.slice(0, route.lastIndexOf("/")) : route;
    const output = await request((detail ? base + "?page_size=1" : base + queryString));
    await Promise.all(output.fields.filter(f => f.resource).map(async f => {
      referenceLabels.set(f.name, await lookup(f.resource));
    }));
    const columns = output.fields.map((field) => field.name)
      .filter((key) => key !== "password" && key !== "is_admin");
    const addCurrency = (row) => {
      if (!row) return;
      for (const key of ["currency", "account_currency"]) {
        if (Object.hasOwn(row, key) && !columns.includes(key)) columns.push(key);
      }
    };
    addCurrency(output.items[0]);
    if (!detail) {
      if (base === "income" || base === "planned-income") root.append(filters(["id_account", "from", "to", "status"]));
      root.append(table(output.items, columns, base, true));
      pageLinks(output);
      if (output.writable) {
        const details = node("details");
        details.className = "editor";
        details.append(node("summary", tr("add")), editor(base, output.fields, null, actor));
        root.append(details);
      }
      return;
    }
    const row = await request(route);
    addCurrency(row);
    root.append(link("/" + base, tr("back")));
    if (base === "accounts/current" || base === "accounts/savings") {
      const history = node("p");
      history.append(link("/" + route + "/history", tr("history")));
      root.append(history);
    }
    root.append(table([row], columns, base));
    if (output.writable) {
      root.append(editor(base, output.fields, row, actor));
      const remove = node("button", tr("remove"));
      remove.className = "danger";
      const error = node("p");
      error.setAttribute("role", "alert");
      remove.addEventListener("click", async () => {
        if (!window.confirm(tr("confirm_remove"))) return;
        remove.disabled = true;
        try {
          await request(route, "DELETE");
          location.assign("/" + base);
        } catch (failure) { error.textContent = failure.message; remove.disabled = false; }
      });
      root.append(remove, error);
    }
  } catch (error) {
    const message = node("p", error.message);
    message.setAttribute("role", "alert");
    root.append(message);
  }
});
