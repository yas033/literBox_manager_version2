const cardOrder = ["temperature", "humidity", "thermal", "odor", "motion", "events"];

function timeLabel(value) {
  if (!value) return "Waiting";
  return new Date(value).toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function renderCards(cards) {
  const container = document.querySelector("#cards");
  container.innerHTML = "";

  for (const key of cardOrder) {
    const card = cards[key];
    const el = document.createElement("article");
    el.className = `metric-card ${card.status}`;
    el.innerHTML = `
      <h3>${card.title}</h3>
      <div class="value">${card.value}</div>
      <div class="detail">${card.detail}</div>
    `;
    container.appendChild(el);
  }
}

function formatNumber(value, suffix = "", digits = 1) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) {
    return "No data";
  }
  return `${Number(value).toFixed(digits)}${suffix}`;
}

function renderRecent(rows) {
  const tbody = document.querySelector("#recentRows");
  tbody.innerHTML = "";

  for (const row of [...rows].reverse()) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${timeLabel(row.timestamp)}</td>
      <td>${formatNumber(row.temperature_c, "C")}</td>
      <td>${formatNumber(row.humidity_percent, "%")}</td>
      <td>${formatNumber(row.amg_max_c, "C")}</td>
      <td>${row.amg_hot_pixels ?? 0}</td>
      <td>${row.mq135_raw}</td>
      <td>${row.motion ? "Detected" : "Clear"}</td>
      <td>${row.motion_event_count}</td>
    `;
    tbody.appendChild(tr);
  }
}

function renderNextSteps(steps) {
  const list = document.querySelector("#nextSteps");
  list.innerHTML = "";

  for (const step of steps) {
    const li = document.createElement("li");
    li.textContent = step;
    list.appendChild(li);
  }
}

async function loadState() {
  const response = await fetch("/api/state", { cache: "no-store" });
  const data = await response.json();

  renderCards(data.cards);
  renderRecent(data.recent);
  renderNextSteps(data.next_steps);

  document.querySelector("#lastUpdate").textContent = timeLabel(data.updated_at);
  document.querySelector("#deviceId").textContent = data.latest?.device_id || "No device";
}

async function seedDemoData() {
  await fetch("/api/seed-demo", { method: "POST" });
  await loadState();
}

document.querySelector("#refreshButton").addEventListener("click", loadState);
document.querySelector("#seedButton").addEventListener("click", seedDemoData);

loadState();
setInterval(loadState, 3000);
