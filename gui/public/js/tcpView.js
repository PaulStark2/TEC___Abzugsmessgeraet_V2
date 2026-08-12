/**
 * G3.5 Three.js TCP viewport — display-only.
 * Core (x,y,z) maps 1:1 to scene with Z-up. No kinematics / interpolation in JS.
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

let renderer;
let scene;
let camera;
let controls;
let tcpMesh;
let linkGeometry;
let linkLine;
let defaultCameraPos;
let defaultTarget;

function initTcpView(canvas) {
  const width = canvas.clientWidth || 640;
  const height = canvas.clientHeight || 360;

  renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setPixelRatio(window.devicePixelRatio || 1);
  renderer.setSize(width, height, false);
  renderer.setClearColor(0x1a1d21, 1);

  scene = new THREE.Scene();

  camera = new THREE.PerspectiveCamera(50, width / height, 0.1, 2000);
  // Machine Z-up: Core (x,y,z) → Three (x,y,z) with camera.up = Z.
  camera.up.set(0, 0, 1);
  defaultCameraPos = new THREE.Vector3(40, -50, 35);
  defaultTarget = new THREE.Vector3(0, 0, 0);
  camera.position.copy(defaultCameraPos);

  controls = new OrbitControls(camera, canvas);
  controls.target.copy(defaultTarget);
  controls.enableDamping = true;
  controls.update();

  scene.add(new THREE.AmbientLight(0xffffff, 0.85));

  // Origin marker
  const origin = new THREE.Mesh(
    new THREE.SphereGeometry(0.35, 12, 12),
    new THREE.MeshBasicMaterial({ color: 0xffffff })
  );
  origin.position.set(0, 0, 0);
  scene.add(origin);

  // Axes: X red, Y green, Z blue (length 25)
  const axisLen = 25;
  scene.add(makeAxis(new THREE.Vector3(axisLen, 0, 0), 0xe74c3c));
  scene.add(makeAxis(new THREE.Vector3(0, axisLen, 0), 0x2ecc71));
  scene.add(makeAxis(new THREE.Vector3(0, 0, axisLen), 0x3498db));

  // TCP as a small point/sphere — pose only from Core status
  tcpMesh = new THREE.Mesh(
    new THREE.SphereGeometry(0.6, 16, 16),
    new THREE.MeshBasicMaterial({ color: 0xffcc00 })
  );
  tcpMesh.position.set(0, 0, 0);
  scene.add(tcpMesh);

  linkGeometry = new THREE.BufferGeometry().setFromPoints([
    new THREE.Vector3(0, 0, 0),
    new THREE.Vector3(0, 0, 0),
  ]);
  linkLine = new THREE.Line(
    linkGeometry,
    new THREE.LineBasicMaterial({ color: 0x9aa0a6 })
  );
  scene.add(linkLine);

  window.addEventListener('resize', onResize);
  requestAnimationFrame(frame);
}

function makeAxis(end, color) {
  const geo = new THREE.BufferGeometry().setFromPoints([
    new THREE.Vector3(0, 0, 0),
    end,
  ]);
  return new THREE.Line(geo, new THREE.LineBasicMaterial({ color }));
}

function onResize() {
  const canvas = renderer.domElement;
  const width = canvas.clientWidth || 640;
  const height = canvas.clientHeight || 360;
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
  renderer.setSize(width, height, false);
}

function frame() {
  requestAnimationFrame(frame);
  controls.update();
  renderer.render(scene, camera);
}

/**
 * Update TCP from Core status only — no interpolation.
 * @param {{x:number,y:number,z:number}} pose
 */
export function setTcpPose(pose) {
  if (!tcpMesh) return;
  const x = Number(pose.x);
  const y = Number(pose.y);
  const z = Number(pose.z);
  if (![x, y, z].every(Number.isFinite)) return;
  // 1:1 Core → scene (Z-up camera)
  tcpMesh.position.set(x, y, z);
  const positions = linkGeometry.attributes.position;
  positions.setXYZ(1, x, y, z);
  positions.needsUpdate = true;
}

export function resetCamera() {
  if (!camera || !controls) return;
  camera.position.copy(defaultCameraPos);
  controls.target.copy(defaultTarget);
  controls.update();
}

export function setupTcpView() {
  const canvas = document.getElementById('tcp-canvas');
  if (!canvas) return;
  initTcpView(canvas);
  const resetBtn = document.getElementById('btn-reset-camera');
  if (resetBtn) {
    resetBtn.addEventListener('click', () => resetCamera());
  }
}
