const char *automationTestApplication = 
	"#include <stdio.h>\n"
	"\n"
	"int main(int argc, char **argv) {\n"
	"\tprintf(\"hello!\\n\");\n"
	"\treturn 0;\n"
	"}\n";

void AutomationSendCommand(const char *command) {
	while (programRunning) UIAutomationProcessMessage();
	UIElementFocus(&textboxInput->e);
	UIAutomationKeyboardType(command);
	while (programRunning) UIAutomationProcessMessage();
}

int UIAutomationRunTests() {
	FILE *f = fopen("hello.c", "wb");
	fwrite(automationTestApplication, 1, strlen(automationTestApplication), f);
	fclose(f);
	system("gcc -g -o hello hello.c");
	sleep(3); // HACK Synchronize with GDB.
	for (int i = 0; i < 10; i++) AutomationSendCommand("echo test\n"); 
	AutomationSendCommand("file hello\n");
	AutomationSendCommand("break 4\n");
	AutomationSendCommand("run\n");
	UI_ASSERT(UIAutomationCheckCodeLineMatches(displayOutput, displayOutput->lineCount, "(gdb) "));
	UI_ASSERT(UIAutomationCheckCodeLineMatches(displayOutput, displayOutput->lineCount - 1, "4		printf(\"hello!\\n\");"));
	UI_ASSERT(UIAutomationCheckCodeLineMatches(displayCode, 4, "\tprintf(\"hello!\\n\");"));
	UI_ASSERT(currentLine == 4);
	UI_ASSERT(((UITable *) InterfaceWindowSwitchToAndFocus("Stack"))->itemCount == 1);
	UI_ASSERT(((UITable *) InterfaceWindowSwitchToAndFocus("Breakpoints"))->itemCount == 1);
	UI_ASSERT(UIAutomationCheckTableItemMatches((UITable *) InterfaceWindowSwitchToAndFocus("Stack"), 0, 0, "0"));
	UI_ASSERT(UIAutomationCheckTableItemMatches((UITable *) InterfaceWindowSwitchToAndFocus("Stack"), 0, 1, "main"));
	UI_ASSERT(UIAutomationCheckTableItemMatches((UITable *) InterfaceWindowSwitchToAndFocus("Stack"), 0, 2, "hello.c:4"));
	UI_ASSERT(UIAutomationCheckTableItemMatches((UITable *) InterfaceWindowSwitchToAndFocus("Breakpoints"), 0, 0, "hello.c"));
	UI_ASSERT(UIAutomationCheckTableItemMatches((UITable *) InterfaceWindowSwitchToAndFocus("Breakpoints"), 0, 1, "4"));
	UIAutomationKeyboardTypeSingle(UI_KEYCODE_FKEY(10), false, false, false);
	while (programRunning) UIAutomationProcessMessage();
	UI_ASSERT(currentLine == 5);
	AutomationSendCommand("c\n");
	UI_ASSERT(((UITable *) InterfaceWindowSwitchToAndFocus("Stack"))->itemCount == 0);
	fprintf(stderr, "UIAutomationRunTests success!\n");
	return 0;
}
