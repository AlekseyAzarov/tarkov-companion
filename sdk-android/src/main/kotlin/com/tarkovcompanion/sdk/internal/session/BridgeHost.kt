package com.tarkovcompanion.sdk.internal.session

import com.tarkovcompanion.sdk.CompanionException
import com.tarkovcompanion.sdk.internal.generated.GeneratedContract
import java.util.concurrent.Executor

/** Where the web flow is, as the core reports it in "bridge_state" actions; kept for process-death restore. */
internal class UiState(val route: String, val query: String?, val itemId: String?) {
    companion object {
        const val ROUTE_SEARCH = GeneratedContract.BRIDGE_ROUTE_SEARCH
        const val ROUTE_ITEM = GeneratedContract.BRIDGE_ROUTE_ITEM
    }
}

/** Called on the executor given to openBridge. */
internal interface BridgeHost {
    fun postProtocolMessageToPage(message: String)

    fun onPageRequestedClose()

    fun onStateChanged(state: UiState)

    // The page will get no answers, e.g. because the native library is missing.
    fun onCoreUnreachable(error: CompanionException)

    fun onPageRequestedSignIn()
}

internal class SerialExecutor(private val backingExecutor: Executor) : Executor {
    private val queuedTasks = ArrayDeque<Runnable>()
    private var runningTask: Runnable? = null

    @Synchronized
    override fun execute(command: Runnable) {
        queuedTasks.addLast(
            Runnable {
                try {
                    command.run()
                } finally {
                    scheduleNextTask()
                }
            },
        )

        if (runningTask == null) {
            scheduleNextTask()
        }
    }

    @Synchronized
    private fun scheduleNextTask() {
        runningTask = queuedTasks.removeFirstOrNull()
        runningTask?.let(backingExecutor::execute)
    }
}
