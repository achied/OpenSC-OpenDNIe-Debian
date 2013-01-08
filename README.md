OpenSC-OpenDNIe para Debian/Ubuntu
==================================

¿Qué es esto?
-------------

* Este repositorio es un fork del proyecto original OpenSC-OpenDNIe (OpenSC con soporte para el DNI electrónico Español)
    * http://github.com/jonsito/OpenSC
    * http://forja.cenatic.es/projects/opendnie

* Se han añadido los helpers Debian
  * Importados desde git://git.debian.org/git/pkg-opensc/opensc.git (2e9effe)
  * Posteriormente modificados (ver debian/changelog) para que el paquete construya de forma correcta.
      * Probado en Debian/Wheezy


Compilación e instalación del paquete (Debian y Ubuntu)
-------------------------------------------------------

1. Instalar dependencias:

   ```bash
   sudo apt-get install build-essential devscripts dh-autoreconf git-buildpackage
   sudo apt-get build-dep opensc
   ```

2. Clonar repositorio con gbp:

   ```bash
   gbp-clone https://github.com/clopez/OpenSC-OpenDNIe-Debian
   cd OpenSC-OpenDNIe-Debian
   ```

3. Compilar el paquete:

   ```bash
   git-buildpackage
   ```

4. Instalarlo:

   ```bash
   sudo dpkg -i ../opensc_0.12.2-OpenDNIe*.deb
   ```

5. Instalar posibles dependencias que falten:

   ```bash
   sudo apt-get install -f
   ```
