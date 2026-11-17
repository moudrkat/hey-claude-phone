package cz.magiak.asistent.local

import android.content.Context
import org.json.JSONObject

/**
 * A 4M-parameter model that lives in the phone (assets/tiny/model.bin, 2 MB). It turns a sentence into a few
 * lines of a small language, and an interpreter runs them; the interpreter, not the model, decides what a
 * program may do. Nothing leaves the phone to think. C side: cpp/tinyhouse.c.
 */
object LocalModel {

    class Result(val program: String, val ok: Boolean, val error: String, val screen: Boolean,
                 val rows: List<Pair<Int, String>>, val said: String)

    @Volatile private var loaded = false

    @Synchronized
    private fun ensure(context: Context): Boolean {
        if (loaded) return true
        System.loadLibrary("tinyhouse")
        val bytes = context.assets.open("tiny/model.bin").use { it.readBytes() }
        loaded = nativeLoad(bytes)
        return loaded
    }

    /** Off the main thread: the model writes for a few hundred milliseconds. */
    @Synchronized
    fun ask(context: Context, sentence: String): Result {
        if (!ensure(context)) return Result("", false, "the local model did not load", false, emptyList(), "")
        // What reaches the model is what it was trained on: plain ASCII, as a speech recogniser would give it.
        val plain = sentence.filter { it.code in 32..126 }.trim()
        val program = nativeWrite(plain)
        if (program.isEmpty()) return Result("", false, "that sentence does not fit", false, emptyList(), "")
        val json = JSONObject(nativeRun(program))
        if (!json.getBoolean("ok")) return Result(program, false, json.getString("error"), false, emptyList(), "")
        val rows = json.getJSONArray("rows").let { a -> (0 until a.length()).map { a.getJSONObject(it).run { getInt("size") to getString("text") } } }
        return Result(program, true, "", json.getBoolean("screen"), rows, json.getString("said"))
    }

    @JvmStatic private external fun nativeLoad(model: ByteArray): Boolean
    @JvmStatic private external fun nativeWrite(sentence: String): String
    @JvmStatic private external fun nativeRun(program: String): String
}
