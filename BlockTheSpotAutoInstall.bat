@echo off
powershell -Command "Invoke-Expression ""& { $(Invoke-WebRequest -UseBasicParsing 'https://raw.githubusercontent.com/kutlime/BlockTheSpot/automatization/install.ps1') } -UninstallSpotifyStoreEdition -UpdateSpotify -RemoveAdPlaceholder"""
exit