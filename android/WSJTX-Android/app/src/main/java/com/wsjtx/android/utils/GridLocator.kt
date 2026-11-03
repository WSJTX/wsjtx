package com.wsjtx.android.utils

import android.location.Location
import kotlin.math.*

object GridLocator {
    fun grid(lat: Double, lon: Double, precision: Int = 4): String {
        var la = lat + 90; var lo = lon + 180
        val lonF = (lo / 20).toInt(); la -= (la / 10).toInt() * 10
        val latF = (la / 10).toInt()
        lo -= lonF * 20
        val lonS = (lo / 2).toInt(); val latS = la.toInt()
        lo -= lonS * 2; la -= latS
        val f = "ABCDEFGHIJKLMNOPQR"
        val base = "${f[lonF]}${f[latF]}$lonS$latS"
        if (precision <= 4) return base
        val lonSub = (lo / 2 * 24).toInt().coerceIn(0, 23)
        val latSub = (la * 24).toInt().coerceIn(0, 23)
        val s = "ABCDEFGHIJKLMNOPQRSTUVWX"
        return base + s[lonSub] + s[latSub]
    }

    fun grid(loc: Location) = grid(loc.latitude, loc.longitude)

    fun coordinate(grid: String): Pair<Double, Double>? {
        val g = grid.uppercase()
        if (g.length < 4) return null
        if (!g[0].isLetter() || !g[1].isLetter() || !g[2].isDigit() || !g[3].isDigit()) return null
        val lonF = g[0] - 'A'; val latF = g[1] - 'A'
        val lonS = g[2] - '0'; val latS = g[3] - '0'
        var lon = lonF * 20.0 + lonS * 2 - 180 + 1.0
        var lat = latF * 10.0 + latS     - 90  + 0.5
        if (g.length >= 6 && g[4].isLetter() && g[5].isLetter()) {
            lon += (g[4] - 'A') * 2.0 / 24 + 1.0 / 24
            lat += (g[5] - 'A') * 1.0 / 24 + 0.5 / 24
        }
        return Pair(lat, lon)
    }

    fun distance(from: String, to: String): Double? {
        val c1 = coordinate(from) ?: return null
        val c2 = coordinate(to)   ?: return null
        val r = 6_371.0
        val dLat = Math.toRadians(c2.first - c1.first)
        val dLon = Math.toRadians(c2.second - c1.second)
        val a = sin(dLat / 2).pow(2) + cos(Math.toRadians(c1.first)) *
                cos(Math.toRadians(c2.first)) * sin(dLon / 2).pow(2)
        return 2 * r * asin(sqrt(a))
    }

    fun isValid(g: String): Boolean {
        val u = g.uppercase()
        if (u.length != 4 && u.length != 6) return false
        if (!u[0].isLetter() || !u[1].isLetter() || !u[2].isDigit() || !u[3].isDigit()) return false
        if (u.length == 6 && (!u[4].isLetter() || !u[5].isLetter())) return false
        return (u[0] - 'A') < 18 && (u[1] - 'A') < 18
    }
}
