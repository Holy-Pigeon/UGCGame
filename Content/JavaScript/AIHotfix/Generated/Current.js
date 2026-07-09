const { argv } = require('puerts');
const bridge = argv.getByName('Bridge');

// Make Buddy circle around the tower at (-800,0,600)
const cx = -800, cy = 0, cz = 600, radius = 300;
const stepsPerLap = 16;          // waypoints per full circle
const intervalMs = 900;          // time between waypoints

if (globalThis.__circleTimer) {
  clearInterval(globalThis.__circleTimer);
  globalThis.__circleTimer = null;
}
globalThis.__circleStep = globalThis.__circleStep || 0;

function sendWaypoint() {
  const i = globalThis.__circleStep % stepsPerLap;
  const ang = (i / stepsPerLap) * Math.PI * 2;
  const x = cx + radius * Math.cos(ang);
  const y = cy + radius * Math.sin(ang);
  try {
    bridge.EmitGameplayCommand('CommandTeammate', JSON.stringify({
      teammate: 'Buddy',
      action: 'move_to',
      x: x, y: y, z: cz,
    }));
  } catch (e) {
    bridge.LogMessage('circle waypoint error: ' + e);
  }
  globalThis.__circleStep++;
}

sendWaypoint();
globalThis.__circleTimer = setInterval(sendWaypoint, intervalMs);
bridge.LogMessage('Buddy circling tower hotfix live');
'circle loop started';
