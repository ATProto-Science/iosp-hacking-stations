// Thompson sampling over named "arms" (e.g. prompt styles, response strategies).
// Lifted near-verbatim from sail-judge.mjs's Bandit class (SAIL / haiku.garden,
// ~/haiku.garden/scripts/chatto-realtime-demo/sail-judge.mjs) — this class was
// already substrate- and domain-agnostic there, so nothing needed to change.
//
// Beta(a,b) = X/(X+Y) for X~Gamma(a,1), Y~Gamma(b,1). Hand-rolled Marsaglia-Tsang
// gamma sampler rather than adding a stats dependency for something this small.

function sampleStandardNormal() {
  const u1 = Math.random();
  const u2 = Math.random();
  return Math.sqrt(-2 * Math.log(u1)) * Math.cos(2 * Math.PI * u2);
}

function sampleGamma(shape) {
  if (shape < 1) {
    const u = Math.random();
    return sampleGamma(1 + shape) * Math.pow(u, 1 / shape);
  }
  const d = shape - 1 / 3;
  const c = 1 / Math.sqrt(9 * d);
  for (;;) {
    let x, v;
    do {
      x = sampleStandardNormal();
      v = 1 + c * x;
    } while (v <= 0);
    v = v * v * v;
    const u = Math.random();
    if (u < 1 - 0.0331 * x * x * x * x) return d * v;
    if (Math.log(u) < 0.5 * x * x + d * (1 - v + Math.log(v))) return d * v;
  }
}

function sampleBeta(alpha, beta) {
  const x = sampleGamma(alpha);
  const y = sampleGamma(beta);
  return x / (x + y);
}

export class Bandit {
  constructor(armKeys, initial = {}) {
    this.arms = Object.fromEntries(
      armKeys.map((k) => [k, initial[k] ? { ...initial[k] } : { alpha: 1, beta: 1 }])
    );
  }
  // Samples every arm's posterior, returns the highest draw plus all draws
  // (useful for logging/teaching — see this repo's demo output).
  choose() {
    let bestKey = null;
    let bestSample = -Infinity;
    const draws = {};
    for (const [key, { alpha, beta }] of Object.entries(this.arms)) {
      const sample = sampleBeta(alpha, beta);
      draws[key] = sample;
      if (sample > bestSample) {
        bestSample = sample;
        bestKey = key;
      }
    }
    return { chosen: bestKey, draws };
  }
  reward(key, success) {
    if (success) this.arms[key].alpha += 1;
    else this.arms[key].beta += 1;
  }
  snapshot() {
    return Object.fromEntries(Object.entries(this.arms).map(([k, v]) => [k, { ...v }]));
  }
}
