const chartBase = {
  responsive: true, maintainAspectRatio: false,
  plugins: { legend: { display: false }, tooltip: { enabled: true } },
  scales: {
    x: { grid: { display: false }, ticks: { color: "#718078", font: { family: "DM Sans" } }, border: { display: false } },
    y: { beginAtZero: true, grid: { color: "#edf1ec" }, ticks: { color: "#718078", font: { family: "DM Sans" } }, border: { display: false } }
  }
};
function makeComparisonChart(id, values, label, color, suffix) {
  const el = document.getElementById(id);
  if (!el) return;
  new Chart(el, {
    type: "bar",
    data: { labels: ["Agricultura vertical", "Agricultura tradicional"], datasets: [{
      label, data: values, backgroundColor: color, borderRadius: 8, borderSkipped: false, maxBarThickness: 65
    }]},
    options: { ...chartBase, plugins: { ...chartBase.plugins, tooltip: { callbacks: { label: c => `${c.parsed.y} ${suffix}` } } } }
  });
}
makeComparisonChart("waterChart", [20, 100], "Consumo de água", ["#29945d", "#9ab4a0"], "L/kg");
makeComparisonChart("yieldChart", [30, 10], "Produção por área", ["#29945d", "#9ab4a0"], "kg/m²");
