"use strict";

document.addEventListener("DOMContentLoaded", async () => {
  const csrf = document.querySelector('meta[name="csrf-token"]').content;
  if (!csrf) return;
  const menu = document.querySelector("#menu");
  try {
    const [response, translationResponse] = await Promise.all([
      fetch("/api/v1/ui/menu"),
      fetch("/api/v1/i18n/" + document.documentElement.lang.replace("-", "_"))
    ]);
    if (response.status === 401) { location.assign("/login"); return; }
    if (!response.ok || !translationResponse.ok) return;
    const items = await response.json();
    const labels = await translationResponse.json();
    const groups = new Map();
    for (const item of items) {
      let target = menu;
      if (item.group && !groups.has(item.group)) {
        const detail = document.createElement("details");
        const summary = document.createElement("summary");
        summary.textContent = item.group;
        const links = document.createElement("div");
        detail.append(summary, links);
        menu.append(detail);
        groups.set(item.group, links);
      }
      if (item.group) target = groups.get(item.group);
      const link = document.createElement("a");
      link.href = item.href;
      link.textContent = item.label;
      if (location.pathname === item.href || (item.href !== "/" && location.pathname.startsWith(item.href + "/"))) {
        link.setAttribute("aria-current", "page");
        if (item.group) target.parentElement.querySelector("summary").classList.add("active");
      }
      target.append(link);
    }
    const status = document.createElement("span");
    status.setAttribute("role", "status");
    const logout = document.createElement("button");
    logout.type = "button";
    logout.textContent = labels.logout;
    logout.className = "quiet";
    logout.addEventListener("click", async () => {
      logout.disabled = true;
      try {
        const reply = await fetch("/api/v1/auth/logout", {method: "POST", headers: {"X-CSRF-Token": csrf}});
        if (reply.ok || reply.status === 401) location.assign("/login");
        else throw new Error();
      } catch {
        status.textContent = labels["errors.internal"];
        logout.disabled = false;
      }
    });
    menu.append(logout, status);
  } catch {
    // The server-rendered page remains available when navigation cannot be fetched.
  }
});
