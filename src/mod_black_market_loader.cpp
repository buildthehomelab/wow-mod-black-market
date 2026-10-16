/*
 * mod-black-market loader.
 *
 * AzerothCore looks up a loader symbol derived from the module's folder name: for folder
 * "mod-black-market" that symbol is exactly "Addmod_black_marketScripts". If you clone the repo
 * under a different folder name, rename this function to match.
 *
 * Released under the MIT License.
 */

void AddBlackMarketScripts();
void AddBlackMarketAddonScripts();

void Addmod_black_marketScripts()
{
    AddBlackMarketScripts();
    AddBlackMarketAddonScripts();
}
