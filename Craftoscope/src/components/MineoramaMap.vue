<template>
    <div class="map" ref="map"></div>
</template>

<script>
    import L from "leaflet";

    L.TileLayer.Mineorama = L.TileLayer.extend({
        options: {
            dimension: 0,
            layer: 255,
            minZoom: -5,
            maxZoom: 6,
            tileSize: 512,
            minNativeZoom: 0,
            maxNativeZoom: 0,
        },
        initialize: function (tileIndex, options) {
            this._tileIndex = tileIndex;
            L.TileLayer.prototype.initialize.call(this, '/tiles/tile_{dimension}_{_displayLayer}_{x}_{y}.png', options);
            L.setOptions(this, options);
        },
        getTileUrl(coords) {
            const limit = this._tileIndex[this.options.dimension][coords.x][coords.y];

            this.options._displayLayer = Math.min(this.options.layer, limit);

            return L.TileLayer.prototype.getTileUrl.call(this, coords);
        },
        _isValidTile(coords) {
            if (!L.TileLayer.prototype._isValidTile.call(this, coords)) {
                return false;
            }

            if (this._tileIndex[this.options.dimension] === undefined) {
                return false;
            }

            if (this._tileIndex[this.options.dimension][coords.x] === undefined) {
                return false;
            }

            if (this._tileIndex[this.options.dimension][coords.x][coords.y] === undefined) {
                return false;
            }

            return true;
        },
        _tileIndex: null,
    });
    
    L.tileLayer.mineorama = function (tileIndex, options) {
        return new L.TileLayer.Mineorama(tileIndex, options);
    };

    L.Control.LayerSlider = L.Control.extend({
        options: {
            dimension: 1,
            tileIndex: null,
            displayLayer: null,
        },
        onAdd(map) {
            /** @type{HTMLDivElement} */
            const container = L.DomUtil.create("div");
            L.DomEvent.disableScrollPropagation(container);
            L.DomEvent.disableClickPropagation(container);

            /** @type{HTMLButtonElement} */
            const overworldButton = L.DomUtil.create("button");
            overworldButton.type = "button";
            overworldButton.textContent = "Overworld";
            container.appendChild(overworldButton);
            L.DomEvent.on(overworldButton, "click", (function() {
                this.options.dimension = 0;
                this._resetMap(map);
            }).bind(this));

            /** @type{HTMLButtonElement} */
            const netherButton = L.DomUtil.create("button");
            netherButton.type = "button";
            netherButton.textContent = "Nether";
            container.appendChild(netherButton);
            L.DomEvent.on(netherButton, "click", (function() {
                this.options.dimension = 1;
                this._resetMap(map);
            }).bind(this));

            /** @type{HTMLInputElement} */
            this._rangeElement = L.DomUtil.create("input");
            this._rangeElement.type = "range";
            this._rangeElement.min = 0;
            this._rangeElement.max = 255;
            this._rangeElement.step = 1;
            this._rangeElement.value = 255;
            this._rangeElement.style.width = "calc(100vw - 25px)";
            container.appendChild(this._rangeElement);
            L.DomEvent.on(this._rangeElement, "input", this._onSliderChange.bind(this, map));

            this._resetMap(map);

            return container;
        },
        _rangeElement: null,
        _resetMap(map) {
            let xBounds = [+Infinity, -Infinity];
            let yBounds = [+Infinity, -Infinity];
            let zBounds = [+Infinity, -Infinity];

            for (const x in this.options.tileIndex[this.options.dimension]) {
                if (!this.options.tileIndex[this.options.dimension].hasOwnProperty(x)) {
                    continue;
                }

                xBounds[0] = Math.min(xBounds[0], x);
                xBounds[1] = Math.max(xBounds[1], x);

                for (const z in this.options.tileIndex[this.options.dimension][x]) {
                    if (!this.options.tileIndex[this.options.dimension][x].hasOwnProperty(z)) {
                        continue;
                    }

                    zBounds[0] = Math.min(zBounds[0], z);
                    zBounds[1] = Math.max(zBounds[1], z);

                    const y = this.options.tileIndex[this.options.dimension][x][z];
                    yBounds[0] = Math.min(yBounds[0], y);
                    yBounds[1] = Math.max(yBounds[1], y);
                }
            }

            this._rangeElement.max = yBounds[1];
            this._rangeElement.value = yBounds[1];

            this._onSliderChange(map);

            const multiplier = 512;
            const bounds = [
                [(zBounds[1] + 1) * -multiplier, xBounds[0] * multiplier],
                [zBounds[0] * -multiplier, (xBounds[1] + 1) * multiplier],
            ];

            /*L.rectangle(bounds, {
                color: "#ff7800",
                weight: 1,
            }).addTo(map);*/

            map.fitBounds(bounds, {
                animate: false,
            });
            // map.setMaxBounds(bounds);
        },
        _onSliderChange(map) {
            const layer = +this._rangeElement.value;
            let oldLayer = this.options.displayLayer;
            this.options.displayLayer = L.tileLayer.mineorama(this.options.tileIndex, {
                dimension: this.options.dimension,
                layer: layer,
            }).on('load', () => {
                setTimeout(() => {
                    if (oldLayer) {
                        oldLayer.remove();
                        oldLayer = null;
                    }
                }, 250);
            }).addTo(map);
        },
    });

    L.control.layerSlider = function (options) {
        return new L.Control.LayerSlider(options);
    };

    L.Control.LayerInspector = L.Control.extend({
        options: {
            colorMap: null,
        },
        onAdd(map) {
            /** @type{HTMLDivElement} */
            const container = L.DomUtil.create("div");

            /*
            map.on("mousemove", (e) => {
                console.log(e);
            });
            */

            return container;
        },
    });

    L.control.layerInspector = function (options) {
        return new L.Control.LayerInspector(options);
    };

    export default {
        name: 'MineoramaMap',
        mounted() {
            this.$options.map = L.map(this.$refs.map, {
                attributionControl: false,
                zoomSnap: 0.5,
                zoomDelta: 1,
                crs: L.CRS.Simple,
                center: [-450, -250],
                zoom: 1,
                fadeAnimation: false,
            });

            fetch('/tiles/index.json')
                .then(response => response.json())
                .then(tileIndex => L.control.layerSlider({
                    position: 'bottomright',
                    tileIndex: tileIndex,
                }).addTo(this.$options.map));

            fetch('/tiles/colors.json')
                .then(response => response.json())
                .then(colorMap => L.control.layerInspector({
                    position: 'topright',
                    colorMap: colorMap,
                }).addTo(this.$options.map));
        },
        map: null,
    };
</script>

<style>
    @import "../../node_modules/leaflet/dist/leaflet.css";

    .leaflet-container {
        background: #000;
    }

    img.leaflet-tile {
        image-rendering: pixelated;
    }
</style>

<style scoped>
    .map {
        width: 100%;
        height: 100%;
    }
</style>
