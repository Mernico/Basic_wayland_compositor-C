<div align="center">

  <h1>Basic Wayland Compositor</h1>

  <p>
    <strong>A lightweight, C-based Wayland compositor prototype built as part of my Bachelor's thesis.</strong>
  </p>

  <p>
    Developed over a two-month period, this project serves as a proof-of-concept for core Wayland window management and input handling rather than a full daily driver.
  </p>

  <p>
    <img src="https://img.shields.io/badge/Language-C-blue?style=for-the-badge&logo=c" alt="Language" />
    <img src="https://img.shields.io/badge/Protocol-Wayland-orange?style=for-the-badge&logo=wayland" alt="Protocol" />
    <img src="https://img.shields.io/badge/Status-Prototype-yellow?style=for-the-badge" alt="Status" />
  </p>

</div>

---

## 🌟 Key Features

### 💻 Client/Window Management

* **Client Open / Close:** Spawning and termination of Wayland surfaces.
  <p align="center">
    <img src="./readmeAssets/F2A.gif" alt="Client Open and Close Demo" width="800" />
  </p>

* **Window Focus:** Visual and input focus switching across open clients.
  <p align="center">
    <img src="./readmeAssets/F4A.gif" alt="Window Focus Demo" width="800" />
  </p>

* **Basic Window Tiling:** Automatic grid-based layout for active windows.
  <p align="center">
    <img src="./readmeAssets/F6A.gif" alt="Window Tiling Demo" width="800" />
  </p>

* **Window Position Switch:** Swapping client positions across the grid.
  <p align="center">
    <img src="./readmeAssets/F7A.gif" alt="Window Position Switch Demo" width="800" />
  </p>

* **Window Resizing:** surface redrawing.
  <p align="center">
    <img src="./readmeAssets/F8A.gif" alt="Window Resize Demo" width="800" />
  </p>

### 🖱️ Input Handling

* **Mouse Cursor Tracking:** Pointer is rendered.
  <p align="center">
    <img src="./readmeAssets/F3A.gif" alt="Mouse Cursor Demo" width="800" />
  </p>

* **Keyboard Functions:** Basic Keyboard logic implementation.
  <p align="center">
    <img src="./readmeAssets/F5A.gif" alt="Keyboard Function Demo" width="800" />
  </p>



