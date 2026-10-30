// Dados demonstrativos. Cada registro representa uma medição fictícia.
const labels = ["08:00", "08:30", "09:00", "09:30", "10:00", "10:30", "11:00", "11:30", "12:00"];
const sample = {
  alpha: {
    humidity: [66, 67, 68, 68, 69, 70, 69, 68, 68],
    light: [320, 350, 390, 420, 450, 470, 480, 465, 460],
    ec: [1.6, 1.7, 1.7, 1.8, 1.8, 1.8, 1.9, 1.8, 1.8]
  },
  beta: {
    humidity: [74, 75, 76, 77, 76, 76, 77, 76, 76],
    light: [290, 315, 340, 360, 370, 390, 400, 390, 380],
    ec: [1.5, 1.5, 1.4, 1.4, 1.4, 1.3, 1.4, 1.4, 1.4]
  }
};
const colors = { alpha: "#29945d", beta: "#568bc3" };
const chartInstances = {};
const commonOptions = {
  responsive: true, maintainAspectRatio: false,
  interaction: { mode: "index", intersect: false },
  plugins: { legend: { position: "bottom", labels: { usePointStyle: true, boxWidth: 7, padding: 18, color: "#68766d", font: { family: "DM Sans", size: 11 } } } },
  scales: {
    x: { grid: { color: "#edf1ec" }, ticks: { color: "#87948b", maxRotation: 0, font: { size: 10 } }, border: { display: false } },
    y: { grid: { color: "#edf1ec" }, ticks: { color: "#87948b", font: { size: 10 } }, border: { display: false }, beginAtZero: true }
  }
};
function lineDataset(name, values, color) {
  return { label: name, data: values, borderColor: color, backgroundColor: color, borderWidth: 2.5, pointRadius: 2, pointHoverRadius: 4, tension: .35 };
}
function createChart(id, key, unit) {
  const ctx = document.getElementById(id);
  if (!ctx) return;
  chartInstances[key] = new Chart(ctx, {
    type: "line",
    data: { labels, datasets: [
      lineDataset("Torre Alfa", sample.alpha[key], colors.alpha),
      lineDataset("Torre Beta", sample.beta[key], colors.beta)
    ]},
    options: { ...commonOptions, plugins: { ...commonOptions.plugins, tooltip: { callbacks: { label: c => `${c.dataset.label}: ${c.parsed.y} ${unit}` } } } }
  });
}
createChart("humidityChart", "humidity", "%");
createChart("lightChart", "light", "µmol/m²/s");
createChart("ecChart", "ec", "mS/cm");

const filter = document.getElementById("towerFilter");
if (filter) filter.addEventListener("change", () => {
  Object.values(chartInstances).forEach(chart => {
    chart.data.datasets.forEach((dataset, index) => {
      dataset.hidden = filter.value === "alpha" ? index === 1 : filter.value === "beta" ? index === 0 : false;
    });
    chart.update();
  });
});

const rows = document.getElementById("measurementRows");
if (rows) {
  const records = [
    ["12:00", "Alfa", "68%", "460", "1.8"],
    ["12:00", "Beta", "76%", "380", "1.4"],
    ["11:30", "Alfa", "68%", "465", "1.8"],
    ["11:30", "Beta", "76%", "390", "1.4"],
    ["11:00", "Alfa", "69%", "480", "1.9"],
    ["11:00", "Beta", "77%", "400", "1.4"]
  ];
  rows.innerHTML = records.map(r => `<tr><td>${r[0]}</td><td>${r[1]}</td><td>${r[2]}</td><td>${r[3]}</td><td>${r[4]}</td></tr>`).join("");
}

// Integração futura: configurar as credenciais do Supabase em um arquivo separado.
// Não coloque a chave service_role no front-end. Use somente a chave anon/public com políticas RLS.
