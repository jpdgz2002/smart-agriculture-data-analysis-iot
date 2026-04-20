# Sensor Capacitivo Portátil para Cacao

**DESARROLLO DE PINZA CAPACITIVA NO INVASIVA PARA LA DETECCIÓN DE CAMBIOS EN MADURACIÓN CAUSADOS POR *Moniliophthora roreri* EN FRUTOS DE CACAO**

Proyecto de Trabajo de Grado - Ingeniería Física  
Universidad EAFIT | 2026  
**Autor:** Juan Pablo Díaz González  
**Asesor:** Dr. David Velásquez Rendón

---

## 📋 Descripción

Prototipo de sensor capacitivo no invasivo portátil que permite detectar de forma objetiva y temprana la presencia de moniliasis (*Moniliophthora roreri*) en frutos de cacao (*Theobroma cacao*), especialmente de la variedad CCN-51.

El sistema mide cambios en la capacitancia dieléctrica del fruto sin dañarlo, alcanzando **TRL 5** (validado en entorno relevante simulado).

### Resultados principales
- Diferencias estadísticamente significativas entre tres estados fitosanitarios (ANOVA de Welch, *p* < 10⁻⁶⁹).
- Capacitancia promedio:
  - Enfermo etapa inicial: **11.29 µF**
  - Maduro sano: **18.75 µF**
  - Maduro contaminado: **28.34 µF**

---

## ✨ Características

- Excitación sinusoidal a 1 kHz mediante DAC ESP32
- Medición RMS con ADS1115 (DFR0553)
- Cálculo de capacitancia en tiempo real usando modelo empírico calibrado
- Almacenamiento local en microSD + transmisión en tiempo real a ThingSpeak
- Interfaz OLED + indicador LED RGB según estado del fruto
- Diseño mecánico de pinza semi-curva ergonómica

---

## 📁 Estructura del Repositorio

| Carpeta              | Contenido |
|----------------------|---------|
| `/firmware/`         | Código fuente del ESP32 (`codigo_final_completo.ino`) |
| `/esquematicos/`     | Esquema eléctrico (Proteus) y diseño de PCB |
| `/3D/`               | Modelos 3D de la PCB y vista explosionada del sensor |
| `/documentacion/`    | Tesis, referencias y resultados experimentales |

---

## 🛠️ Cómo usar

1. Clonar el repositorio
2. Abrir `firmware/codigo_final_completo.ino` en Arduino IDE
3. Configurar tus credenciales WiFi y canal de ThingSpeak
4. Subir el firmware al ESP32
5. Colocar el sensor sobre el fruto y observar los valores de capacitancia en el OLED y en ThingSpeak

---

## 📊 Datos Experimentales

Los datos en tiempo real de las pruebas se encuentran en el canal público de ThingSpeak:  
[https://thingspeak.mathworks.com/channels/3040125](https://thingspeak.mathworks.com/channels/3040125)

---

## 📄 Referencias

- Trabajo de Grado completo (PDF) disponible en la carpeta `/documentacion/`
- Artículo relacionado publicado en *Smart Innovation, Systems and Technologies* (Springer, 2025)

---

**Licencia:** MIT  
**Cita sugerida:** Díaz-González, J.P. (2026). *Mejora de sistema capacitivo no invasivo hacia la detección de cambios en maduración causados por Moniliophthora roreri en frutos de cacao*. Trabajo de Grado, Universidad EAFIT.

---

⭐ Si este repositorio te es útil, ¡dale una estrella!
