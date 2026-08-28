/**
 * M15 gantry viewport — display only.
 *
 * The pose is supplied by the controller (Core status). This module contains no
 * kinematics, no interpolation and no motion logic; it only maps (x, y, z) onto
 * a hierarchical scene graph:
 *
 *   railsGroup (static)
 *   bridgeGroup   .position.x = x   (Y traverse + carriage + column + TCP)
 *     carriageGroup .position.y = y (column + TCP)
 *       columnGroup   .position.z = z (Z column slides through the carriage, TCP at its lower end)
 *
 * Kept free of page-specific code so it can be embedded in the machine GUI later.
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const DEFAULT_GEOMETRY = {
  sizeX: 200,
  sizeY: 150,
  sizeZ: 120,
  profile: 12,
  toolDiameter: 2.5,
  toolCylindricalHeight: 15,
};

const COLORS = {
  background: 0x12151a,
  rails: 0x6f7783,
  bridge: 0x5b9fd4,
  carriage: 0x9aa0a6,
  column: 0x8fa8bd,
  tcp: 0xffb300,
  floor: 0x2a2f37,
  limit: 0xe57373,
  probeDisc: 0xf0c040,
  probeStem: 0x90a4ae,
  probeBase: 0x546e7a,
};

function isFinitePose(pose) {
  return (
    pose &&
    Number.isFinite(pose.x) &&
    Number.isFinite(pose.y) &&
    Number.isFinite(pose.z)
  );
}

function overlaySignature(value) {
  try {
    return JSON.stringify(value);
  } catch {
    return '';
  }
}

function box(width, depth, height, color) {
  return new THREE.Mesh(
    new THREE.BoxGeometry(width, depth, height),
    new THREE.MeshStandardMaterial({ color, metalness: 0.15, roughness: 0.7 })
  );
}

/**
 * @param {HTMLCanvasElement} canvas
 * @param {{ sizeX?: number, sizeY?: number, sizeZ?: number, profile?: number }} [geometry]
 */
export function createGantryView(canvas, geometry = {}) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setPixelRatio(window.devicePixelRatio || 1);
  renderer.setClearColor(COLORS.background, 1);

  const scene = new THREE.Scene();

  // Machine convention is Z-up; Core (x, y, z) maps 1:1 to scene units (mm).
  const camera = new THREE.PerspectiveCamera(45, 16 / 10, 1, 20000);
  camera.up.set(0, 0, 1);

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;

  scene.add(new THREE.AmbientLight(0xffffff, 0.55));
  const keyLight = new THREE.DirectionalLight(0xffffff, 0.85);
  keyLight.position.set(1, -1.4, 2);
  scene.add(keyLight);
  const fillLight = new THREE.DirectionalLight(0x9fc4e0, 0.35);
  fillLight.position.set(-1.5, 1, 0.6);
  scene.add(fillLight);

  // Static frame and moving assemblies are rebuilt whenever geometry changes.
  const staticGroup = new THREE.Group();
  const bridgeGroup = new THREE.Group();
  const carriageGroup = new THREE.Group();
  const columnGroup = new THREE.Group();
  const limitGroup = new THREE.Group();
  const testPointGroup = new THREE.Group();
  const referenceMarkerGroup = new THREE.Group();
  const plannedTrajectoryGroup = new THREE.Group();
  const traveledTrajectoryGroup = new THREE.Group();
  const probeGroup = new THREE.Group();
  const overlayLabelGroup = new THREE.Group();

  scene.add(staticGroup);
  scene.add(bridgeGroup);
  scene.add(limitGroup);
  scene.add(testPointGroup);
  scene.add(referenceMarkerGroup);
  scene.add(plannedTrajectoryGroup);
  scene.add(traveledTrajectoryGroup);
  scene.add(probeGroup);
  scene.add(overlayLabelGroup);
  bridgeGroup.add(carriageGroup);
  carriageGroup.add(columnGroup);

  let config = { ...DEFAULT_GEOMETRY, ...geometry };
  const pose = { x: 0, y: 0, z: 0 };
  let running = true;
  let showLimitMarkers = true;
  let showTrajectory = true;
  let storedLimits = null;
  const overlaySigs = {
    testPoints: '',
    referenceMarkers: '',
    plannedSegments: '',
    traveledSamples: '',
    overlayLabels: '',
  };

  function skipUnchangedOverlay(key, value) {
    const sig = overlaySignature(value);
    if (sig === overlaySigs[key]) {
      return true;
    }
    overlaySigs[key] = sig;
    return false;
  }

  function clearGroup(group) {
    for (const child of [...group.children]) {
      group.remove(child);
      if (child.geometry) {
        child.geometry.dispose();
      }
      if (child.material) {
        child.material.dispose();
      }
    }
  }

  function clearOverlay(group) {
    for (const child of [...group.children]) {
      group.remove(child);
      child.material?.map?.dispose();
      child.material?.dispose();
      child.geometry?.dispose();
    }
  }

  function makeTextSprite(text, tint) {
    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d');
    const font = '600 22px Consolas, "Courier New", monospace';
    ctx.font = font;
    const lines = String(text).split('\n');
    const textWidth = Math.max(...lines.map((line) => ctx.measureText(line).width), 8);
    const padX = 10;
    const lineH = 26;
    const padY = 8;
    canvas.width = Math.ceil(textWidth + padX * 2);
    canvas.height = Math.ceil(lines.length * lineH + padY * 2);
    ctx.font = font;
    ctx.fillStyle = 'rgba(18, 21, 26, 0.78)';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = 'rgba(90, 159, 212, 0.35)';
    ctx.strokeRect(0.5, 0.5, canvas.width - 1, canvas.height - 1);
    ctx.fillStyle = tint || '#e8eef5';
    lines.forEach((line, i) => {
      ctx.fillText(line, padX, padY + (i + 1) * lineH - 6);
    });
    const texture = new THREE.CanvasTexture(canvas);
    const material = new THREE.SpriteMaterial({
      map: texture,
      transparent: true,
      depthTest: false,
      sizeAttenuation: true,
    });
    const sprite = new THREE.Sprite(material);
    const worldW = Math.min(28, Math.max(12, canvas.width * 0.08));
    sprite.scale.set(worldW, worldW * (canvas.height / canvas.width), 1);
    sprite.renderOrder = 10;
    return sprite;
  }

  // Remove meshes but keep the hierarchy groups (they carry the pose).
  function clearMeshes(group, keep) {
    for (const child of [...group.children]) {
      if (child === keep) {
        continue;
      }
      group.remove(child);
      child.geometry?.dispose();
      child.material?.dispose();
    }
  }

  function buildGeometry() {
    clearGroup(staticGroup);
    clearMeshes(bridgeGroup, carriageGroup);
    clearMeshes(carriageGroup, columnGroup);
    clearGroup(columnGroup);

    const { sizeX, sizeY, sizeZ, profile } = config;
    const p = profile;
    const bridgeHeight = sizeZ + 3 * p;

    // Floor plate below the workspace (visual reference only).
    const floor = box(sizeX + 6 * p, sizeY + 6 * p, p / 2, COLORS.floor);
    floor.position.set(sizeX / 2, sizeY / 2, -p - p / 4);
    staticGroup.add(floor);

    // Two X rails on the floor; they never move.
    for (const railY of [0, sizeY]) {
      const rail = box(sizeX + 2 * p, p, p, COLORS.rails);
      rail.position.set(sizeX / 2, railY, -p / 2);
      staticGroup.add(rail);
    }

    // Uprights carry the Y traverse; they travel with the bridge along X.
    for (const postY of [0, sizeY]) {
      const post = box(p, p, bridgeHeight, COLORS.rails);
      post.position.set(0, postY, bridgeHeight / 2);
      bridgeGroup.add(post);
    }

    // Y traverse spanning both rails at bridge height.
    const traverse = box(p, sizeY + 2 * p, p, COLORS.bridge);
    traverse.position.set(0, sizeY / 2, bridgeHeight);
    bridgeGroup.add(traverse);

    // Carriage riding on the traverse; it guides the Z column.
    const carriage = box(1.8 * p, 1.8 * p, 1.4 * p, COLORS.carriage);
    carriage.position.set(0, 0, bridgeHeight);
    carriageGroup.add(carriage);

    // Z column slides vertically through the carriage. Its length is chosen so
    // that it always reaches through the carriage: the assembly cannot separate.
    const columnLength = sizeZ + 6 * p;
    const column = box(0.8 * p, 0.8 * p, columnLength, COLORS.column);
    column.position.set(0, 0, columnLength / 2 + p);
    columnGroup.add(column);

    // Spreader: vertical cylinder (TCP = lower face) + small TCP sphere Ø≈2 mm.
    const toolDia = Number.isFinite(config.toolDiameter) ? config.toolDiameter : 2.5;
    const toolH = Number.isFinite(config.toolCylindricalHeight) ? config.toolCylindricalHeight : 15;
    const toolRadius = Math.max(0.2, toolDia / 2);
    const tool = new THREE.Mesh(
      new THREE.CylinderGeometry(toolRadius, toolRadius, toolH, 20),
      new THREE.MeshStandardMaterial({ color: COLORS.tcp, metalness: 0.35, roughness: 0.45 })
    );
    tool.rotation.x = Math.PI / 2;
    tool.position.set(0, 0, toolH / 2);
    columnGroup.add(tool);

    const tcpBall = new THREE.Mesh(
      new THREE.SphereGeometry(1.0, 14, 14),
      new THREE.MeshStandardMaterial({ color: 0xffeb3b, emissive: 0x554400, metalness: 0.2, roughness: 0.4 })
    );
    tcpBall.position.set(0, 0, 0);
    columnGroup.add(tcpBall);

    const grid = new THREE.GridHelper(
      Math.max(sizeX, sizeY) * 1.6,
      12,
      0x3a404a,
      0x2a2f37
    );
    grid.rotation.x = Math.PI / 2;
    grid.position.set(sizeX / 2, sizeY / 2, -p * 1.6);
    staticGroup.add(grid);

    const axes = new THREE.AxesHelper(Math.max(sizeX, sizeY, sizeZ) * 0.25);
    staticGroup.add(axes);

    applyPose();
  }

  function applyPose() {
    bridgeGroup.position.x = pose.x;
    carriageGroup.position.y = pose.y;
    columnGroup.position.z = pose.z;
  }

  function resetCamera() {
    const { sizeX, sizeY, sizeZ } = config;
    const span = Math.max(sizeX, sizeY, sizeZ);
    controls.target.set(sizeX / 2, sizeY / 2, sizeZ / 2);
    camera.position.set(sizeX / 2 + span * 1.3, -span * 1.9, sizeZ + span * 1.2);
    controls.update();
  }

  function resize() {
    const width = canvas.clientWidth || canvas.width;
    const height = canvas.clientHeight || canvas.height;
    if (width === 0 || height === 0) {
      return;
    }
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }

  function animate() {
    if (!running) {
      return;
    }
    controls.update();
    renderer.render(scene, camera);
    window.requestAnimationFrame(animate);
  }

  buildGeometry();
  resetCamera();
  resize();
  animate();

  window.addEventListener('resize', resize);

  return {
    /**
     * Apply a controller pose. Non-finite input is ignored, so the last known
     * pose simply freezes (offline behaviour).
     * @param {{ x: number, y: number, z: number }} next
     */
    setPose(next) {
      if (!isFinitePose(next)) {
        return;
      }
      pose.x = next.x;
      pose.y = next.y;
      pose.z = next.z;
      applyPose();
    },

    /** Rebuild the machine frame (display geometry only). */
    setGeometry(next) {
      config = { ...config, ...next };
      buildGeometry();
      resetCamera();
    },

    /** Update spreader size from MachineConfig without resetting the camera. */
    setToolGeometry(next) {
      const dia = Number(next && next.toolDiameter);
      const height = Number(next && next.toolCylindricalHeight);
      if (!Number.isFinite(dia) || !Number.isFinite(height)) {
        return;
      }
      if (config.toolDiameter === dia && config.toolCylindricalHeight === height) {
        return;
      }
      config.toolDiameter = dia;
      config.toolCylindricalHeight = height;
      buildGeometry();
    },

    /** Show the configured limit switch trip planes (6 positions). */
    setLimits(limits) {
      storedLimits = limits;
      rebuildLimitMarkers();
    },

    setShowLimitMarkers(visible) {
      showLimitMarkers = Boolean(visible);
      limitGroup.visible = showLimitMarkers;
    },

    setShowTrajectory(visible) {
      showTrajectory = Boolean(visible);
      plannedTrajectoryGroup.visible = showTrajectory;
      traveledTrajectoryGroup.visible = showTrajectory;
      overlaySigs.plannedSegments = '';
      overlaySigs.traveledSamples = '';
    },

    setTestPointsVisible(visible) {
      testPointGroup.visible = Boolean(visible);
    },

    setTestPoints(points) {
      if (skipUnchangedOverlay('testPoints', points || [])) {
        return;
      }
      clearGroup(testPointGroup);
      if (!Array.isArray(points)) {
        return;
      }
      for (const point of points) {
        if (!Number.isFinite(point.x) || !Number.isFinite(point.y) || !Number.isFinite(point.z)) {
          continue;
        }
        // Ø ≈ 2 mm (radius 1 mm), independent of gantry profile.
        const sphere = new THREE.Mesh(
          new THREE.SphereGeometry(1.0, 12, 12),
          new THREE.MeshStandardMaterial({ color: 0x66bb6a, emissive: 0x224422 })
        );
        sphere.position.set(point.x, point.y, point.z);
        testPointGroup.add(sphere);
      }
    },

    /**
     * Core-state reference markers (never SimPlant::ProbeDisc.center as probe_center).
     * @param {{
     *   home?: {x:number,y:number,z:number}|null,
     *   plateOrigin?: {x:number,y:number,z:number}|null,
     *   probeCenter?: {x:number,y:number,z:number}|null,
     *   showHome?: boolean,
     *   showPlate?: boolean,
     *   showProbe?: boolean,
     * }} markers
     */
    setReferenceMarkers(markers = {}) {
      if (skipUnchangedOverlay('referenceMarkers', markers || {})) {
        return;
      }
      clearGroup(referenceMarkerGroup);
      const addMarker = (pose, color, axisLen = 8) => {
        if (!pose || ![pose.x, pose.y, pose.z].every(Number.isFinite)) {
          return;
        }
        const ball = new THREE.Mesh(
          new THREE.SphereGeometry(1.2, 12, 12),
          new THREE.MeshStandardMaterial({ color, emissive: color, emissiveIntensity: 0.25 })
        );
        ball.position.set(pose.x, pose.y, pose.z);
        referenceMarkerGroup.add(ball);
        const axes = new THREE.AxesHelper(axisLen);
        axes.position.set(pose.x, pose.y, pose.z);
        referenceMarkerGroup.add(axes);
      };
      if (markers.showHome !== false) {
        addMarker(markers.home, 0x42a5f5);
      }
      if (markers.showPlate !== false) {
        addMarker(markers.plateOrigin, 0xab47bc);
      }
      if (markers.showProbe !== false) {
        addMarker(markers.probeCenter, 0xef5350);
      }
    },

    setPlannedTrajectory(points) {
      clearGroup(plannedTrajectoryGroup);
      if (!showTrajectory || !Array.isArray(points) || points.length < 2) {
        return;
      }
      const verts = [];
      for (const p of points) {
        verts.push(p.x, p.y, p.z);
      }
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
      const line = new THREE.Line(
        geometry,
        new THREE.LineDashedMaterial({ color: 0x90caf9, dashSize: 2, gapSize: 1 })
      );
      line.computeLineDistances();
      plannedTrajectoryGroup.add(line);
    },

    /**
     * Draw Core Safe-Z approach segments (raise / XY / lower) as separate polylines.
     * @param {Array<{type:string, from:{x,y,z}, to:{x,y,z}}>} segments
     */
    setPlannedSegments(segments) {
      if (skipUnchangedOverlay('plannedSegments', segments || [])) {
        return;
      }
      clearGroup(plannedTrajectoryGroup);
      if (!showTrajectory || !Array.isArray(segments) || segments.length === 0) {
        return;
      }
      const colorFor = (type) => {
        if (type === 'SafeZ') return 0xffb74d;
        if (type === 'XY') return 0x90caf9;
        if (type === 'Z' || type === 'ZCoarse') return 0xce93d8;
        if (type === 'ZFine') return 0xab47bc;
        if (type === 'ZMeasure') return 0x7e57c2;
        return 0xb0bec5;
      };
      for (const seg of segments) {
        if (!seg?.from || !seg?.to) {
          continue;
        }
        const verts = [seg.from.x, seg.from.y, seg.from.z, seg.to.x, seg.to.y, seg.to.z];
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
        const line = new THREE.Line(
          geometry,
          new THREE.LineBasicMaterial({ color: colorFor(seg.type) })
        );
        plannedTrajectoryGroup.add(line);
      }
    },

    setTraveledTrajectory(completedPoints, currentPose) {
      clearGroup(traveledTrajectoryGroup);
      if (!showTrajectory) {
        return;
      }
      const path = [...(completedPoints || [])];
      if (currentPose && Number.isFinite(currentPose.x)) {
        path.push(currentPose);
      }
      if (path.length < 2) {
        return;
      }
      const verts = [];
      for (const p of path) {
        verts.push(p.x, p.y, p.z);
      }
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
      traveledTrajectoryGroup.add(
        new THREE.Line(geometry, new THREE.LineBasicMaterial({ color: 0x43a047 }))
      );
    },

    /** Append-only TCP samples for traveled path (drawn in raw / plant frame). */
    setTraveledSamples(samples) {
      if (skipUnchangedOverlay('traveledSamples', samples || [])) {
        return;
      }
      clearGroup(traveledTrajectoryGroup);
      if (!showTrajectory || !Array.isArray(samples) || samples.length < 2) {
        return;
      }
      const verts = [];
      for (const p of samples) {
        if (!Number.isFinite(p.x) || !Number.isFinite(p.y) || !Number.isFinite(p.z)) {
          continue;
        }
        verts.push(p.x, p.y, p.z);
      }
      if (verts.length < 6) {
        return;
      }
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
      traveledTrajectoryGroup.add(
        new THREE.Line(geometry, new THREE.LineBasicMaterial({ color: 0x43a047 }))
      );
    },

    /**
     * Sprite labels for the same points/segments already drawn.
     * @param {Array<{x:number,y:number,z:number,text:string,kind?:string}>} labels
     */
    setOverlayLabels(labels) {
      if (skipUnchangedOverlay('overlayLabels', labels || [])) {
        return;
      }
      clearOverlay(overlayLabelGroup);
      if (!Array.isArray(labels)) {
        return;
      }
      for (const item of labels) {
        if (!item || !Number.isFinite(item.x) || !Number.isFinite(item.y) || !Number.isFinite(item.z)) {
          continue;
        }
        const tint = item.kind === 'segment' ? '#ffcc80' : '#c5e1a5';
        const sprite = makeTextSprite(item.text || '', tint);
        sprite.position.set(item.x, item.y, item.z + (item.kind === 'segment' ? 2.5 : 3.5));
        overlayLabelGroup.add(sprite);
      }
    },

    /**
     * Visual measuring head: disc + stem + base. Collision uses disc geometry only.
     * @param {{ x: number, y: number, z: number, diameter?: number, thickness?: number }} disc
     */
    setProbeDisc(disc) {
      clearGroup(probeGroup);
      if (!disc || !Number.isFinite(disc.x) || !Number.isFinite(disc.y) || !Number.isFinite(disc.z)) {
        return;
      }
      const diameter = Number.isFinite(disc.diameter) && disc.diameter > 0 ? disc.diameter : 8;
      const thickness = Number.isFinite(disc.thickness) && disc.thickness > 0 ? disc.thickness : 3;
      const radius = diameter / 2;

      const plate = new THREE.Mesh(
        new THREE.CylinderGeometry(radius, radius, thickness, 32),
        new THREE.MeshStandardMaterial({ color: COLORS.probeDisc, metalness: 0.25, roughness: 0.45 })
      );
      plate.rotation.x = Math.PI / 2;
      plate.position.set(disc.x, disc.y, disc.z);
      probeGroup.add(plate);

      const discBottom = disc.z - thickness / 2;
      const stemTop = Math.max(discBottom, 0.5);
      const stemLength = Math.max(stemTop - 0.5, 1);
      const stem = new THREE.Mesh(
        new THREE.CylinderGeometry(Math.min(1.2, radius * 0.35), Math.min(1.2, radius * 0.35), stemLength, 12),
        new THREE.MeshStandardMaterial({ color: COLORS.probeStem, metalness: 0.2, roughness: 0.6 })
      );
      stem.rotation.x = Math.PI / 2;
      stem.position.set(disc.x, disc.y, 0.5 + stemLength / 2);
      probeGroup.add(stem);

      const base = box(Math.max(diameter * 1.6, 12), Math.max(diameter * 1.6, 12), 1.5, COLORS.probeBase);
      base.position.set(disc.x, disc.y, 0.75);
      probeGroup.add(base);
    },

    resetCamera,
    resize,

    dispose() {
      running = false;
      window.removeEventListener('resize', resize);
      controls.dispose();
      renderer.dispose();
    },
  };

  function rebuildLimitMarkers() {
    clearGroup(limitGroup);
    limitGroup.visible = showLimitMarkers;
    const limits = storedLimits;
    if (!limits) {
      return;
    }
    const { sizeX, sizeY, sizeZ } = config;
    const material = () =>
      new THREE.MeshBasicMaterial({
        color: COLORS.limit,
        transparent: true,
        opacity: 0.07,
        depthWrite: false,
        side: THREE.DoubleSide,
      });

    const addPlane = (width, height, position, rotation) => {
      const plane = new THREE.Mesh(new THREE.PlaneGeometry(width, height), material());
      plane.position.copy(position);
      if (rotation) {
        plane.rotation.copy(rotation);
      }
      limitGroup.add(plane);
      const outline = new THREE.LineSegments(
        new THREE.EdgesGeometry(plane.geometry),
        new THREE.LineBasicMaterial({ color: COLORS.limit, transparent: true, opacity: 0.5 })
      );
      outline.position.copy(plane.position);
      outline.rotation.copy(plane.rotation);
      limitGroup.add(outline);
    };

    if (Number.isFinite(limits.xMin)) {
      addPlane(sizeY, sizeZ, new THREE.Vector3(limits.xMin, sizeY / 2, sizeZ / 2), new THREE.Euler(0, Math.PI / 2, 0));
    }
    if (Number.isFinite(limits.xMax)) {
      addPlane(sizeY, sizeZ, new THREE.Vector3(limits.xMax, sizeY / 2, sizeZ / 2), new THREE.Euler(0, Math.PI / 2, 0));
    }
    if (Number.isFinite(limits.yMin)) {
      addPlane(sizeX, sizeZ, new THREE.Vector3(sizeX / 2, limits.yMin, sizeZ / 2), new THREE.Euler(Math.PI / 2, 0, 0));
    }
    if (Number.isFinite(limits.yMax)) {
      addPlane(sizeX, sizeZ, new THREE.Vector3(sizeX / 2, limits.yMax, sizeZ / 2), new THREE.Euler(Math.PI / 2, 0, 0));
    }
    if (Number.isFinite(limits.zMin)) {
      addPlane(sizeX, sizeY, new THREE.Vector3(sizeX / 2, sizeY / 2, limits.zMin), null);
    }
    if (Number.isFinite(limits.zMax)) {
      addPlane(sizeX, sizeY, new THREE.Vector3(sizeX / 2, sizeY / 2, limits.zMax), null);
    }
  }
}
