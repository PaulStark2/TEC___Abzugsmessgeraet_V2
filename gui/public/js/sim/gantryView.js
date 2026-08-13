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
};

function isFinitePose(pose) {
  return (
    pose &&
    Number.isFinite(pose.x) &&
    Number.isFinite(pose.y) &&
    Number.isFinite(pose.z)
  );
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

  scene.add(staticGroup);
  scene.add(bridgeGroup);
  scene.add(limitGroup);
  bridgeGroup.add(carriageGroup);
  carriageGroup.add(columnGroup);

  let config = { ...DEFAULT_GEOMETRY, ...geometry };
  const pose = { x: 0, y: 0, z: 0 };
  let running = true;

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

    // TCP at the lower end of the Z column; its tip sits exactly on the pose.
    const tcp = new THREE.Mesh(
      new THREE.ConeGeometry(0.55 * p, 2 * p, 20),
      new THREE.MeshStandardMaterial({ color: COLORS.tcp, metalness: 0.3, roughness: 0.4 })
    );
    // Rotate the cone so it points along -Z: the tip lands on the pose (local z = 0).
    tcp.rotation.x = -Math.PI / 2;
    tcp.position.set(0, 0, p);
    columnGroup.add(tcp);

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

    /** Show the configured limit switch trip planes. */
    setLimits(limits) {
      clearGroup(limitGroup);
      if (!limits) {
        return;
      }
      const { sizeX, sizeY, sizeZ } = config;
      // Faint markers only: they must not hide the mechanics.
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

      if (Number.isFinite(limits.xMax)) {
        addPlane(
          sizeY,
          sizeZ,
          new THREE.Vector3(limits.xMax, sizeY / 2, sizeZ / 2),
          new THREE.Euler(0, Math.PI / 2, 0)
        );
      }
      if (Number.isFinite(limits.yMax)) {
        addPlane(
          sizeX,
          sizeZ,
          new THREE.Vector3(sizeX / 2, limits.yMax, sizeZ / 2),
          new THREE.Euler(Math.PI / 2, 0, 0)
        );
      }
      if (Number.isFinite(limits.zMax)) {
        addPlane(sizeX, sizeY, new THREE.Vector3(sizeX / 2, sizeY / 2, limits.zMax), null);
      }
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
}
