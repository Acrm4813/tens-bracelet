# TENS Bracelet

Pulsera TENS controlada por Bluetooth: firmware ESP32 (BLE) + PWA con Web Bluetooth.

## Estructura

```
tens-bracelet/
├── tens-bracelet.code-workspace   (abrir con VS Code)
├── netlify.toml
├── README.md
├── .gitignore
├── firmware/                      -> SOLO para el ESP32 (USB)
│   ├── platformio.ini
│   └── esp32_firmware/
│       └── esp32_firmware.ino
└── docs/                          -> SOLO para la web (Netlify)
    ├── index.html
    ├── manifest.webmanifest
    ├── sw.js
    └── icon.svg
```

## 1. Abrir en VS Code

1. Archivo -> Abrir area de trabajo desde archivo -> `tens-bracelet.code-workspace`.
2. Instala las extensiones recomendadas: **PlatformIO IDE** y **Live Server**.

## 2. Cargar el firmware (ESP32)

1. Conecta el ESP32 por USB (cable de datos). Desconecta bateria/TP4056/MT3608 mientras flasheas.
2. Barra azul inferior de PlatformIO: flecha -> **Upload**.
3. Si aparece "Wrong boot mode": manten presionado **BOOT** cuando salga `Connecting....`.
4. Icono de enchufe -> Monitor serie (115200). Debe decir "todos los reles abiertos" y "BLE anunciando como TENS_Bracelet".
5. Prueba por el monitor serie escribiendo `1`, `2`, `3`, `S`, `0`.

## 3. Probar la web en local

Clic derecho en `docs/index.html` -> **Open with Live Server** (http://127.0.0.1:5500). Web Bluetooth funciona en localhost.

## 4. Subir a GitHub

```bash
cd tens-bracelet
git init
git add .
git commit -m "TENS Bracelet: firmware BLE + PWA"
git branch -M main
git remote add origin https://github.com/TU_USUARIO/tens-bracelet.git
git push -u origin main
```

## 5. Desplegar en Netlify (HTTPS automatico)

1. https://app.netlify.com -> Add new site -> Import an existing project -> GitHub -> elige el repo.
2. Build command: vacio. Publish directory: `docs` (ya lo define `netlify.toml`).
3. Deploy. URL: `https://TU-SITIO.netlify.app`.
4. Alternativa sin Git: Add new site -> Deploy manually -> arrastra solo la carpeta `docs/`.

## 6. Usar

1. Abre la URL en **Chrome o Edge** (Android, Windows, macOS, Linux). iOS/Safari no soporta Web Bluetooth.
2. Activa Bluetooth (y ubicacion en Android).
3. Pulsa **Conectar pulsera** y elige `TENS_Bracelet`. No aparece en los ajustes Bluetooth del telefono; solo desde la web.
4. Android: menu de Chrome -> Instalar app.

## Seguridad de hardware

- Pull-ups de 10 kOhm de GPIO 16/17/18/19 a 3.3 V (los pines flotan durante el reinicio).
- Verifica que los reles se apaguen con 3.3 V; si no, usa JD-VCC o adaptador de nivel.
- La LiPo debe pasar por un TP4056 **con proteccion** (DW01 + FS8205A).
- Baja la intensidad del TENS antes de cambiar de electrodo.
- No colocar electrodos en pecho, cuello o cabeza. Contraindicado con marcapasos.
