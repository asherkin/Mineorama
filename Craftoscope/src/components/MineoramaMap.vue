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
            maxZoom: 7,
            minNativeZoom: 0,
            maxNativeZoom: 0,
        },
        initialize: function (tileIndex, options) {
            this._tileIndex = tileIndex;
            L.TileLayer.prototype.initialize.call(this, '/tiles/tile_{dimension}_{_displayLayer}_{x}_{y}.png', options)
            L.setOptions(this, options);
        },
        getTileUrl(coords) {
            if (this._tileIndex[this.options.dimension] === undefined) {
                return '';
            }

            if (this._tileIndex[this.options.dimension][coords.x] === undefined) {
                return '';
            }

            if (this._tileIndex[this.options.dimension][coords.x][coords.y] === undefined) {
                return '';
            }

            const limit = this._tileIndex[this.options.dimension][coords.x][coords.y];

            this.options._displayLayer = Math.min(this.options.layer, limit);

            return L.TileLayer.prototype.getTileUrl.call(this, coords);
        },
        _tileIndex: null,
    });
    
    L.tileLayer.mineorama = function (tileIndex, options) {
        return new L.TileLayer.Mineorama(tileIndex, options);
    };

    L.Control.LayerSlider = L.Control.extend({
        options: {
            tileIndex: null,
            displayLayer: null,
        },
        onAdd() {
            /** @type{HTMLDivElement} */
            const container = L.DomUtil.create("div");
            L.DomEvent.disableScrollPropagation(container);
            L.DomEvent.disableClickPropagation(container);

            /** @type{HTMLInputElement} */
            this._rangeElement = L.DomUtil.create("input");
            this._rangeElement.type = "range";
            this._rangeElement.min = "0";
            this._rangeElement.max = "255";
            this._rangeElement.step = "1";
            this._rangeElement.value = "255";
            this._rangeElement.style.width = "calc(100vw - 25px)";
            container.appendChild(this._rangeElement);
            L.DomEvent.on(this._rangeElement, "input", this._onSliderChange.bind(this));

            this._onSliderChange();

            return container;
        },
        _rangeElement: null,
        _onSliderChange() {
            const layer = +this._rangeElement.value;
            let oldLayer = this.options.displayLayer;
            this.options.displayLayer = L.tileLayer.mineorama(this.options.tileIndex, {
                layer: layer,
            }).on('load', () => {
                setTimeout(() => {
                    if (oldLayer) {
                        oldLayer.remove();
                        oldLayer = null;
                    }
                }, 250);
            }).addTo(this._map);
        },
    });

    L.control.layerSlider = function (options) {
        return new L.Control.LayerSlider(options);
    };

    export default {
        name: 'MineoramaMap',
        mounted() {
            this.$options.map = L.map(this.$refs.map, {
                attributionControl: false,
                zoomSnap: 0.5,
                zoomDelta: 1,
                crs: L.CRS.Simple,
                center: [-234, -137],
                zoom: 2,
                fadeAnimation: false,
            });

            fetch('/tiles/index.json')
                .then(response => response.json())
                .then(tileIndex => L.control.layerSlider({
                    position: 'bottomright',
                    tileIndex: tileIndex,
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