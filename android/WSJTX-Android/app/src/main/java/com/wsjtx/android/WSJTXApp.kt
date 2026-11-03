package com.wsjtx.android

import android.app.Application
import android.content.Context

class WSJTXApp : Application() {
    companion object {
        lateinit var context: Context
            private set
    }

    override fun onCreate() {
        super.onCreate()
        context = applicationContext
    }
}
