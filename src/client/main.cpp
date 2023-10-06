#include "std_include.hpp"
#include "window.hpp"

int main(int argc, char** argv)
{
	window window(800, 600, "game");

	window.show();

	return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main(__argc, __argv);
}
