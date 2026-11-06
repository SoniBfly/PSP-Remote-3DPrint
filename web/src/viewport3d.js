/**
 * Vanilla Three.js viewport for FLSUN Load UI.
 * Printer frame is Z-up; content lives under a −90° X group so Three stays Y-up.
 * STL via three-slicer-viewer/loaders; toolpath via /gcode + /toolpath.
 */
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { loadModel } from 'three-slicer-viewer/loaders'
import { parseGcode } from 'three-slicer-viewer/gcode'
import {
  buildSegmentData,
  makeToolpath,
  computeColors,
} from 'three-slicer-viewer/toolpath'

const BED_R = 130
const BED_H = 360

export class Viewport3D {
  /**
   * @param {HTMLElement} el
   */
  constructor(el) {
    this.el = el
    try {
      this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false })
    } catch (e) {
      this.renderer = new THREE.WebGLRenderer({
        antialias: false,
        alpha: false,
        powerPreference: 'low-power',
        failIfMajorPerformanceCaveat: false,
      })
    }
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2))
    this.renderer.setClearColor(0x0a0a12, 1)
    el.appendChild(this.renderer.domElement)

    this.scene = new THREE.Scene()
    this.camera = new THREE.PerspectiveCamera(45, 1, 1, 2000)
    this.camera.position.set(220, 180, 220)

    this.controls = new OrbitControls(this.camera, this.renderer.domElement)
    this.controls.enableDamping = true
    this.controls.dampingFactor = 0.08
    this.controls.target.set(0, 40, 0)
    this.controls.maxPolarAngle = Math.PI * 0.49
    this.controls.minDistance = 40
    this.controls.maxDistance = 900

    const amb = new THREE.AmbientLight(0xffffff, 0.55)
    const dir = new THREE.DirectionalLight(0xffffff, 0.85)
    dir.position.set(120, 220, 80)
    this.scene.add(amb, dir)

    /* Printer Z-up → Three Y-up */
    this.root = new THREE.Group()
    this.root.rotation.x = -Math.PI / 2
    this.scene.add(this.root)

    this.bed = this._makeBed(BED_R)
    this.root.add(this.bed)

    this.meshGroup = new THREE.Group()
    this.root.add(this.meshGroup)
    this.mesh = null
    this.meshScale = 1

    this.toolpathHandle = null
    this.toolpathGroup = new THREE.Group()
    this.root.add(this.toolpathGroup)

    this._layerCount = 0
    this._raf = 0
    this._ro = new ResizeObserver(() => this._resize())
    this._ro.observe(el)
    this._resize()
    this._loop()
  }

  _makeBed(r) {
    const g = new THREE.Group()
    const disc = new THREE.Mesh(
      new THREE.CircleGeometry(r, 72),
      new THREE.MeshStandardMaterial({
        color: 0x1a1a22,
        metalness: 0.15,
        roughness: 0.85,
        side: THREE.DoubleSide,
      }),
    )
    disc.position.z = -0.2
    g.add(disc)

    const ring = new THREE.LineLoop(
      new THREE.BufferGeometry().setFromPoints(
        Array.from({ length: 73 }, (_, i) => {
          const a = (i / 72) * Math.PI * 2
          return new THREE.Vector3(Math.cos(a) * r, Math.sin(a) * r, 0.05)
        }),
      ),
      new THREE.LineBasicMaterial({ color: 0x3a7 }),
    )
    g.add(ring)

    const grid = new THREE.GridHelper(r * 2, 26, 0x2a2a38, 0x1e1e28)
    grid.rotation.x = Math.PI / 2
    grid.position.z = 0.02
    g.add(grid)

    /* Height pole at +X */
    const pole = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(r, 0, 0),
        new THREE.Vector3(r, 0, BED_H),
      ]),
      new THREE.LineBasicMaterial({ color: 0x445 }),
    )
    g.add(pole)
    return g
  }

  _resize() {
    const w = Math.max(1, this.el.clientWidth)
    const h = Math.max(1, this.el.clientHeight)
    this.camera.aspect = w / h
    this.camera.updateProjectionMatrix()
    this.renderer.setSize(w, h, false)
  }

  _loop = () => {
    this._raf = requestAnimationFrame(this._loop)
    this.controls.update()
    this.renderer.render(this.scene, this.camera)
  }

  /**
   * @param {ArrayBuffer} buf
   * @param {string} name
   */
  async loadStl(buf, name) {
    this.clearToolpath()
    const objs = await loadModel(name || 'model.stl', buf)
    if (!objs.length) throw new Error('empty STL')
    const pos = objs[0].modelPos.slice()
    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3))
    geo.computeVertexNormals()
    geo.computeBoundingBox()
    const bb = geo.boundingBox
    const cx = (bb.min.x + bb.max.x) * 0.5
    const cy = (bb.min.y + bb.max.y) * 0.5
    const z0 = bb.min.z
    geo.translate(-cx, -cy, -z0)
    geo.computeBoundingBox()

    const mat = new THREE.MeshStandardMaterial({
      color: 0x6ec1ff,
      emissive: 0x123050,
      metalness: 0.08,
      roughness: 0.45,
      side: THREE.DoubleSide,
    })
    this.clearMesh()
    this.mesh = new THREE.Mesh(geo, mat)
    const edges = new THREE.LineSegments(
      new THREE.EdgesGeometry(geo, 35),
      new THREE.LineBasicMaterial({ color: 0xc8e6ff }),
    )
    this.mesh.add(edges)
    this.meshGroup.add(this.mesh)
    this.setMeshScale(this.meshScale)
    this.fitToObject(this.meshGroup)
  }

  /** @param {number} scalePct */
  setMeshScale(scalePct) {
    this.meshScale = (scalePct || 100) / 100
    this.meshGroup.scale.setScalar(this.meshScale)
  }

  clearMesh() {
    if (this.mesh) {
      this.mesh.traverse((o) => {
        if (o.geometry) o.geometry.dispose()
        if (o.material) o.material.dispose()
      })
      this.meshGroup.remove(this.mesh)
      this.mesh = null
    }
  }

  setMeshVisible(v) {
    this.meshGroup.visible = !!v
  }

  /**
   * @param {string} gcodeText
   * @param {{ lineWidth?: number, hideMesh?: boolean }} [opts]
   */
  showToolpath(gcodeText, opts = {}) {
    this.clearToolpath()
    const { layers } = parseGcode(gcodeText, {
      filamentDiameter: 1.75,
      defaultLayerHeight: 0.2,
    })
    if (!layers.length) return 0
    const lw = opts.lineWidth || 0.4
    const data = buildSegmentData(layers, lw)
    const handle = makeToolpath(THREE, data)
    const colors = computeColors(data, 'feature', {})
    handle.setColors(colors.color)
    handle.setTravelVisible(false)
    this.toolpathHandle = handle
    this.toolpathGroup.add(handle.mesh)
    if (handle.travLines) this.toolpathGroup.add(handle.travLines)
    this._layerCount = handle.layerCount || data.layerCount || 0
    if (this._layerCount > 0)
      handle.setLayerRange(0, this._layerCount - 1)
    if (opts.hideMesh !== false) this.setMeshVisible(false)
    this.fitToBBox(data.bbox)
    return this._layerCount
  }

  clearToolpath() {
    if (this.toolpathHandle) {
      this.toolpathGroup.remove(this.toolpathHandle.mesh)
      if (this.toolpathHandle.travLines)
        this.toolpathGroup.remove(this.toolpathHandle.travLines)
      this.toolpathHandle.dispose()
      this.toolpathHandle = null
    }
    this._layerCount = 0
  }

  layerCount() {
    return this._layerCount
  }

  /** @param {number} lo @param {number} hi inclusive */
  setLayerRange(lo, hi) {
    if (!this.toolpathHandle || this._layerCount <= 0) return
    const a = Math.max(0, Math.min(lo, this._layerCount - 1))
    const b = Math.max(a, Math.min(hi, this._layerCount - 1))
    this.toolpathHandle.setLayerRange(a, b)
  }

  setTravelVisible(v) {
    if (this.toolpathHandle) this.toolpathHandle.setTravelVisible(!!v)
  }

  fitToObject(obj) {
    const box = new THREE.Box3().setFromObject(obj)
    if (box.isEmpty()) {
      this.fitHome()
      return
    }
    const size = box.getSize(new THREE.Vector3())
    const center = box.getCenter(new THREE.Vector3())
    const maxDim = Math.max(size.x, size.y, size.z, 30)
    const dist = Math.max(maxDim * 2.4, 90)
    this.controls.target.copy(center)
    this.camera.position.set(
      center.x + dist * 0.7,
      center.y + dist * 0.55,
      center.z + dist * 0.7,
    )
    this.controls.update()
  }

  /** bbox in printer XYZ (Z-up) from SegmentData */
  fitToBBox(bbox) {
    if (!bbox) {
      this.fitHome()
      return
    }
    const [x0, y0, z0] = bbox.min
    const [x1, y1, z1] = bbox.max
    /* Map printer → three via root rotation: (x,y,z) → (x,z,-y) */
    const min = new THREE.Vector3(x0, z0, -y1)
    const max = new THREE.Vector3(x1, z1, -y0)
    const box = new THREE.Box3(min, max)
    const size = box.getSize(new THREE.Vector3())
    const center = box.getCenter(new THREE.Vector3())
    const maxDim = Math.max(size.x, size.y, size.z, 30)
    const dist = Math.max(maxDim * 2.2, 90)
    this.controls.target.copy(center)
    this.camera.position.set(
      center.x + dist * 0.75,
      center.y + dist * 0.5,
      center.z + dist * 0.75,
    )
    this.controls.update()
  }

  fitHome() {
    this.controls.target.set(0, 40, 0)
    this.camera.position.set(220, 180, 220)
    this.controls.update()
  }

  reset() {
    this.clearToolpath()
    this.clearMesh()
    this.setMeshVisible(true)
    this.fitHome()
  }

  dispose() {
    cancelAnimationFrame(this._raf)
    this._ro.disconnect()
    this.clearToolpath()
    this.clearMesh()
    this.controls.dispose()
    this.renderer.dispose()
    if (this.renderer.domElement.parentNode)
      this.renderer.domElement.parentNode.removeChild(this.renderer.domElement)
  }
}
